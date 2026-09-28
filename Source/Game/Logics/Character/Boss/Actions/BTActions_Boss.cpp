// ボス用BTアクションの実装。
//   ボス固有のコンポーネント(BossCommand)は BTContext から受け取らず、
//   各アクションが world/entity から自分で取得する（BTエンジンを汎用に保つため）。
//
// 技の型（WaitAction など）はヘッダに出さない。外から呼ぶのは RegisterAll だけで、
// 技はレジストリ経由で引く、という決まりをヘッダの形で示すため。
#include "Game/Logics/Character/Boss/Actions/BTActions_Boss.h"

#include "Game/Logics/AI/BehaviorTree/Actions/BTAction.h"
#include "Game/Logics/AI/BehaviorTree/Actions/BTActionRegistry.h"
#include "Game/Logics/AI/BehaviorTree/Data/ActionRegistry.h"
#include "Game/Logics/Character/Boss/Components/BossCommandComponent.h"
#include "Game/Logics/Character/Boss/Components/BossActionComponent.h" // 詰めるのを止める距離
#include "Game/Logics/AI/Drone/DroneComponent.h"
#include "Game/Logics/AI/Drone/Formation/FormationRegistry.h"    // 陣形が持つ時間割
#include "Engine/ECS/Core/CCL_World.h"                           // world->GetComponent
#include "Engine/Animation/Components/AnimParametersComponent.h" // SetTrigger（近接アニメ）
#include "Engine/Core/Math/StringHash.h"                         // HashString

#include <cmath> // 枠番号は param（float）で来るので丸める

namespace CCL::AI::BT
{
    /// 自分が出した行動要求だけを取り下げる。別の行動が既に書き換えていたら触らない
    /// （同じフレームに次の技が出した要求を消してしまわないため）。
    void ClearCommandIfMine(BTContext &ctx, ActionID expected)
    {
        if (auto *cmd = ctx.world->GetComponent<BossCommandComponent>(ctx.entity))
            if (cmd->currentActionId == expected) cmd->currentActionId = 0;
    }

    /// 足が地面から離れているか。跳躍の途中で要求を取り下げてよいかの判断に使う。
    ///
    /// 溜め（Windup）の間はまだ地面にいるので、打ち切っても破綻しない。
    bool IsAirborne(BTContext &ctx)
    {
        const auto *act = ctx.world->GetComponent<BossActionComponent>(ctx.entity);
        return act && act->currentState == BossActionState::Leap && act->leapPhase != BossLeapPhase::Windup;
    }

    /// ① 待機（汎用：bt の nodeTimers だけ使う）。待ち時間はノードの param（秒）。
    struct WaitAction : public IBTAction
    {
        BTNodeState Execute(BTContext &ctx) override
        {
            auto &timers = ctx.bt->nodeTimers;
            if (ctx.nodeId >= timers.size())
                return BTNodeState::Failure;

            if (timers[ctx.nodeId] == -1.0f)
                timers[ctx.nodeId] = ctx.param;

            if (timers[ctx.nodeId] > 0.0f)
                return BTNodeState::Running;

            return BTNodeState::Success;
        }
    };

    /// ② 蓄積ダメージのリセット（汎用：bt のみ）。
    struct ResetDamageAction : public IBTAction
    {
        BTNodeState Execute(BTContext &ctx) override
        {
            ctx.bt->blackboard.accumulatedDamage = 0.0f;
            return BTNodeState::Success;
        }
    };

    /// ②-2 被弾後の様子見（攻撃ヒット時待ち時間）。大原則は「被弾は今の行動を中断せず、
    /// 次に何をするかの判断にだけ影響する」。
    ///
    /// Selector の先頭に置いても技を潰さないのは、Selector が Running 中の子を先に再開
    /// するから。技を出している間はこの枝まで評価が降りてこない。
    /// 怒り状態で OFF にするには木からこのノードを外すか param を 0 にする。
    struct HitWaitAction : public IBTAction
    {
        BTNodeState Execute(BTContext &ctx) override
        {
            ctx.bt->lastLogActionId = A_BossHitWait;

            // 自分のタイマーを持たず、知覚側の値を param と見比べるだけ。
            // 途中でもう一発殴られたら自動で延びる。
            return (ctx.bt->blackboard.timeSinceLastHit < ctx.param) ? BTNodeState::Running
                                                                     : BTNodeState::Failure;
        }
    };

    /// ③ 移動（Move）。意図を currentActionId に書き、0.5s で一旦 Success させて
    /// BT に再評価させる。
    struct MoveAction : public IBTAction
    {
        // world から直接書くので申告する（BTAction.h の説明を参照）。
        std::vector<CCL::ECS::TypeID> GetExtraWriteTypes() const override
        {
            return TypeIDsOf<BossCommandComponent>();
        }

        BTNodeState Execute(BTContext &ctx) override
        {
            auto &timers = ctx.bt->nodeTimers;
            if (ctx.nodeId >= timers.size())
                return BTNodeState::Failure;

            ctx.bt->lastLogActionId = A_Move;

            // 完了したら要求を取り下げる。消さないと BossActionSystem が最後の依頼を握った
            // ままになり、次にボス本体の行動が選ばれるまでその姿勢で固まる。
            if (timers[ctx.nodeId] == 0.0f)
            {
                ClearCommandIfMine(ctx, A_Move);
                return BTNodeState::Success;
            }

            if (timers[ctx.nodeId] == -1.0f)
                timers[ctx.nodeId] = 0.5f;

            if (auto *cmd = ctx.world->GetComponent<BossCommandComponent>(ctx.entity))
            {
                cmd->currentActionId = A_Move;
            }
            return BTNodeState::Running;
        }

        // 打ち切られると上の「完了したら取り下げる」を通らない。歩く要求が残ったままだと、
        // 割り込み先が要求を書かない技（様子見など）のとき、ボスは歩き続ける。
        void OnAbort(BTContext &ctx) override { ClearCommandIfMine(ctx, A_Move); }
    };

    /// ③-2 詰めながら待つ（Advance）。技と技の間に入れる「間」で、A_Wait と差し替えて使う。
    /// 技の後ろに A_Wait を素で置くと、8〜40m の帯でボスは一歩も動かず陣形だけを飛ばす
    /// 置物になる（A_Move は「距離 > 40m」の枝にしかなく、上の枝が先に通るため）。
    ///
    /// 近づき切っても Success にはせず、止めるのは足だけで param 秒最後まで Running。
    /// 途中で完了させると間の長さがプレイヤーの位置で変わり、後隙が読めなくなる。
    struct AdvanceAction : public IBTAction
    {
        std::vector<CCL::ECS::TypeID> GetExtraWriteTypes() const override
        {
            return TypeIDsOf<BossCommandComponent>();
        }

        std::vector<CCL::ECS::TypeID> GetExtraReadTypes() const override
        {
            return TypeIDsOf<BossActionComponent>();
        }

        BTNodeState Execute(BTContext &ctx) override
        {
            auto &timers = ctx.bt->nodeTimers;
            if (ctx.nodeId >= timers.size()) return BTNodeState::Failure;

            ctx.bt->lastLogActionId = A_BossAdvance;

            if (timers[ctx.nodeId] == 0.0f)
            {
                ClearCommandIfMine(ctx, A_Move);
                return BTNodeState::Success;
            }

            if (timers[ctx.nodeId] == -1.0f)
                timers[ctx.nodeId] = (ctx.param > 0.0f) ? ctx.param : 2.0f;

            float stopDistance = 7.0f; // BossActionComponent が無いときの仮の間合い
            if (const auto *act = ctx.world->GetComponent<BossActionComponent>(ctx.entity))
                stopDistance = act->advanceStopDistance;

            auto *cmd = ctx.world->GetComponent<BossCommandComponent>(ctx.entity);
            if (!cmd) return BTNodeState::Running;

            if (ctx.bt->blackboard.distanceToTarget > stopDistance)
                cmd->currentActionId = A_Move; // 歩く（旋回も ProcessMove がやる）
            else
                ClearCommandIfMine(ctx, A_Move); // 足を止めて構える（待機旋回へ戻る）

            return BTNodeState::Running;
        }

        void OnAbort(BTContext &ctx) override { ClearCommandIfMine(ctx, A_Move); }
    };

    /// ④ ドローン陣形の指示（2000番台）。「この形を作れ」と指示して即完了する。
    /// 予告の長さも硬直の長さも持たず、技1回分はBTグラフの並びで表す。
    ///
    ///     [陣形] [Wait 予告] [Strike] [Wait 攻撃＋硬直] [帰投] [Wait 次までの間]
    ///
    /// 一度は時間割を陣形クラス側へ移したが、数字の出どころが C++ と BT の2か所に分かれ、
    /// 両方が攻撃信号を立てて同じ技が2度当たる不具合を出した。調整する数字はエディタ側へ。
    struct DroneAction : public IBTAction
    {
        ActionID id; // どの陣形指示か（2000番台）。DRONE_ACTION_LIST で陣形へ引く
        explicit DroneAction(ActionID actionId) : id(actionId) {}

        std::vector<CCL::ECS::TypeID> GetExtraWriteTypes() const override
        {
            return TypeIDsOf<BossCommandComponent>();
        }

        BTNodeState Execute(BTContext &ctx) override
        {
            ctx.bt->lastLogActionId = id;

            DroneFormationType formation = DroneFormationType::Hidden;
            if (id == A_DroneFormation)
            {
                // データの技は param の枠番号で指す。名前で指さないのは、エディタで名前を
                // 変えた瞬間に BT が技を見失うため（枠番号は技の識別子）。
                const int slot = static_cast<int>(std::lround(ctx.param));
                if (slot <= 0 || slot >= static_cast<int>(kDroneFormationSlotCount))
                    return BTNodeState::Failure;
                formation = static_cast<DroneFormationType>(slot);
            }
            // C++ で書いた陣形は BT_DRONE_ACTION_LIST が唯一の対応表。
            else if (!TryGetDroneFormation(id, formation))
                return BTNodeState::Failure;

            auto *cmd = ctx.world->GetComponent<BossCommandComponent>(ctx.entity);
            if (!cmd) return BTNodeState::Failure;

            // 攻撃信号を下ろすのもここの役目。前の技の「今撃て」が残っていると、
            // 組み上がる途中の陣形がそのまま突っ込む。
            cmd->requestFormation  = formation;
            cmd->droneAttackActive = false;
            return BTNodeState::Success;
        }
    };

    /// ⑤ ドローン攻撃リリース（Strike）。「今撃て」信号を立て、DroneCommandSystem 経由で
    /// 各ドローンが FireCharge へ入る。[陣形] の後ろにこれが無い枝は形を作って帰るだけに
    /// なる（読み込み時に BehaviorTreeLoader が見張っている）。下ろすのは次の陣形指示の役目。
    struct DroneStrikeAction : public IBTAction
    {
        std::vector<CCL::ECS::TypeID> GetExtraWriteTypes() const override
        {
            return TypeIDsOf<BossCommandComponent>();
        }

        BTNodeState Execute(BTContext &ctx) override
        {
            ctx.bt->lastLogActionId = A_DroneStrike;
            if (auto *cmd = ctx.world->GetComponent<BossCommandComponent>(ctx.entity))
            {
                cmd->droneAttackActive = true;
            }
            return BTNodeState::Success;
        }
    };

    /// ⑥ トリガー駆動の技：アニメのトリガーを引いて param 秒 BT を留める。
    /// AIは「技を決めてトリガーを引く」だけで、当たり判定のタイミングはアニメクリップ内の
    /// HitBox イベントが握り、物理挙動は BossActionSystem が担当する。
    ///
    /// 「近接攻撃」ではなく「トリガー駆動」なのは、後方ステップも同じ形だから。
    /// 違いは triggerHash と物理側だけ。前提は triggerHash に対応する遷移があること。
    struct TriggeredMotionAction : public IBTAction
    {
        uint32_t triggerHash; // 開始フレームに引くアニメのトリガー名のハッシュ

        float defaultDuration; // ノードの param が未設定(0)のときに使う技の全体時間 [秒]

        ActionID id; // この技の ActionID。currentActionId に書き、終わったら取り下げる
        TriggeredMotionAction(uint32_t hash, float fallbackDur, ActionID actionId)
            : triggerHash(hash), defaultDuration(fallbackDur), id(actionId) {}

        // AnimParametersComponent の申告が本命。抜けていると、トリガーを消費する
        // AnimControllerSystem と並列に走り、同一の vector へ push_back と erase が入る。
        std::vector<CCL::ECS::TypeID> GetExtraWriteTypes() const override
        {
            return TypeIDsOf<BossCommandComponent, AnimParametersComponent>();
        }

        // CanAbort が跳躍の段を読む。
        std::vector<CCL::ECS::TypeID> GetExtraReadTypes() const override
        {
            return TypeIDsOf<BossActionComponent>();
        }

        BTNodeState Execute(BTContext &ctx) override
        {
            auto &timers = ctx.bt->nodeTimers;
            if (ctx.nodeId >= timers.size()) return BTNodeState::Failure;

            ctx.bt->lastLogActionId = id;

            // 硬直終了。MoveAction と同じ理由で要求を取り下げる。
            if (timers[ctx.nodeId] == 0.0f)
            {
                ClearCommandIfMine(ctx, id);
                return BTNodeState::Success;
            }

            // 開始フレーム：アニメのトリガーを1回だけ引く。
            if (timers[ctx.nodeId] == -1.0f)
            {
                timers[ctx.nodeId] = (ctx.param > 0.0f) ? ctx.param : defaultDuration;
                if (auto *anim = ctx.world->GetComponent<AnimParametersComponent>(ctx.entity))
                    anim->SetTrigger(triggerHash);
            }

            // どの技を出しているかを BossActionSystem へ伝える。
            if (auto *cmd = ctx.world->GetComponent<BossCommandComponent>(ctx.entity))
                cmd->currentActionId = id;

            return BTNodeState::Running;
        }

        // 空中にいる間は打ち切らせない。要求が下りると体を動かす側が待機へ倒れ、
        // 放物線の途中から重力で落ちる。着地した最初のフレームには許可が出る。
        bool CanAbort(BTContext &ctx) const override { return !IsAirborne(ctx); }

        void OnAbort(BTContext &ctx) override { ClearCommandIfMine(ctx, id); }
    };

    /// ボス用の行動をすべて BTActionRegistry へ登録する。起動時に1回だけ呼ぶ。
    void RegisterBossActions()
    {
        auto &reg = BTActionRegistry::Instance();

        // 各技の秒数は「BTがこの技に留まる全体時間」＝後隙の長さ。段ごとの内訳は
        // BossActionComponent 側で調整する。ノードに param があればそちらが勝つ。

        // 後方ステップ：後退0.35 + 硬直。攻撃判定は持たない。硬直をステップ本体より長く
        // 取るのは、0だと「殴られる→即離脱→即接近」を繰り返して手を出す隙が消えるため。
        reg.RegisterActionInstance(A_BossBackStep,
                                   std::make_unique<TriggeredMotionAction>(CCL::Utils::HashString("BackStep"), 0.9f, A_BossBackStep));

        // 中央へ跳んで逃げる：溜め0.25 + 滞空1.1 + 着地硬直（LeapLand クリップ 0.63秒）。
        // 全体時間が溜め＋滞空より短いと空中で要求が下りて待機へ戻り、重力で落ちる。
        // 跳躍系はどれも同じ "Leap" を引き、着地までの絵は IsLeaping（BossAnimSyncSystem）が運ぶ。
        reg.RegisterActionInstance(A_BossLeapEscape,
                                   std::make_unique<TriggeredMotionAction>(CCL::Utils::HashString("Leap"), 1.9f, A_BossLeapEscape));

        // 跳躍攻撃：溜め0.35 + 滞空0.8 + 着地硬直。既定の全体時間は、攻撃判定を持つ着地
        // クリップ（Ultimate_Land・1.47秒）が終わるまでの長さにしてある。短くすると
        // 判定が出ている最中に次の技が始まる。
        // 攻撃の着地クリップへの切り替えは LeapAttack（BossAnimSyncSystem）が運ぶので、
        // 引くトリガーは逃げる跳躍と同じ。
        reg.RegisterActionInstance(A_BossLeapAttack,
                                   std::make_unique<TriggeredMotionAction>(CCL::Utils::HashString("Leap"), 2.6f, A_BossLeapAttack));

        // その場で滞空：溜め0.3 + 上昇下降0.9 + 頂点で2.5 + 着地硬直（LeapLand・0.63秒）。
        // 短くすると頂点に居るまま要求が下りて、待機へ戻った瞬間に重力で落ちる。
        // ドローンの陣形攻撃は Parallel の従に置き、滞空（主）が全体の長さを決める。
        reg.RegisterActionInstance(A_BossAirHover,
                                   std::make_unique<TriggeredMotionAction>(CCL::Utils::HashString("Leap"), 4.4f, A_BossAirHover));

        reg.RegisterAction<WaitAction>(A_Wait);

        reg.RegisterAction<ResetDamageAction>(A_ResetDamage);
        reg.RegisterAction<MoveAction>(A_Move);
        reg.RegisterAction<DroneStrikeAction>(A_DroneStrike);
        reg.RegisterAction<HitWaitAction>(A_BossHitWait);
        reg.RegisterAction<AdvanceAction>(A_BossAdvance);

        // データの技はこの1つ。どの技かは枠番号（param）が決めるので、技が増えても触らない。
        reg.RegisterActionInstance(A_DroneFormation, std::make_unique<DroneAction>(A_DroneFormation));

        // C++ で書いた陣形も同じ DroneAction。表から直接登録するので技を足しても触らない
        // （連番ループだと欠番も登録してしまう）。
#define CCL_REGISTER_DRONE_ACTION(Name, Id, Formation, Role, Display) reg.RegisterActionInstance(Name, std::make_unique<DroneAction>(Name));
        BT_DRONE_ACTION_LIST(CCL_REGISTER_DRONE_ACTION)
#undef CCL_REGISTER_DRONE_ACTION
    }
} // namespace CCL::AI::BT
