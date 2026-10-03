// ボス固有の Component(BossCommand など) は BTContext から受け取らず、
// 各アクションが world と entity から自分で取る（BT の実行部を汎用に保つため）。
//
// アクションの型（WaitAction など）は無名 namespace に置き、ヘッダに出さない。
// 外から呼ぶのは RegisterBossActions だけで、アクションはレジストリ経由で引く。
#include "Game/Logics/Character/Boss/Actions/BTActions_Boss.h"

#include "Game/Logics/AI/BehaviorTree/Actions/BTAction.h"
#include "Game/Logics/AI/BehaviorTree/Actions/BTActionRegistry.h"
#include "Game/Logics/AI/BehaviorTree/Data/ActionRegistry.h"
#include "Game/Logics/Character/Boss/Components/BossCommandComponent.h"
#include "Game/Logics/Character/Boss/Components/BossActionComponent.h" // 詰めるのを止める距離
#include "Game/Logics/AI/Drone/Components/DroneComponent.h"
#include "Game/Logics/AI/Drone/Formation/FormationRegistry.h"    // 陣形が持つ時間割
#include "Engine/ECS/Core/CCL_World.h"                           // world->GetComponent
#include "Engine/Animation/Components/AnimParametersComponent.h" // SetTrigger（近接アニメ）
#include "Engine/Core/Math/StringHash.h"                         // HashString

#include <cmath> // 枠番号は param（float）で来るので丸める

namespace CCL::AI::BT
{
    namespace
    {
    /**
     * @brief 自分が出した行動要求だけを取り下げる。
     *
     * 別の行動が既に書き換えていたら触らない。同じフレームに次の技が出した要求を消してしまわないため。
     *
     * @param ctx      BT の実行文脈。
     * @param expected 自分が出した行動の ID。currentActionId がこれと一致するときだけ 0 に戻す。
     */
    void ClearCommandIfMine(BTContext &ctx, ActionID expected)
    {
        if (auto *cmd = ctx.world->GetComponent<BossCommandComponent>(ctx.entity))
            if (cmd->currentActionId == expected) cmd->currentActionId = 0;
    }

    /**
     * @brief 足が地面から離れているかを返す。技の途中で要求を取り下げてよいかの判断に使う。
     *
     * 溜め（Windup）の間はまだ地面にいるので、打ち切っても問題ない。
     *
     * 突進は下がり始めから浮き、突っ込み終わって地面に着くまでが空中。
     * 着地後の前進（Travel 以降）は地面を走っているだけなので、打ち切っても落ちない。
     *
     * @param ctx BT の実行文脈。
     * @return 空中なら true。BossActionComponent が無ければ false。
     */
    bool IsAirborne(BTContext &ctx)
    {
        const auto *act = ctx.world->GetComponent<BossActionComponent>(ctx.entity);
        if (!act) return false;

        if (act->currentState == BossActionState::Leap) return act->leapPhase != BossLeapPhase::Windup;
        if (act->currentState == BossActionState::Dash)
        {
            return act->dashPhase == BossDashPhase::Retreat || act->dashPhase == BossDashPhase::Aim ||
                   act->dashPhase == BossDashPhase::Windup || act->dashPhase == BossDashPhase::Charge;
        }
        return false;
    }

    // 待機。待ち時間はノードの param（秒）。使うのは bt の nodeTimers だけ。
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

    // 蓄積ダメージを 0 に戻す。
    struct ResetDamageAction : public IBTAction
    {
        BTNodeState Execute(BTContext &ctx) override
        {
            ctx.bt->blackboard.accumulatedDamage = 0.0f;
            return BTNodeState::Success;
        }
    };

    // 被弾後の様子見。被弾しても今の行動は中断せず、次に何をするかの判断にだけ影響させる。
    //
    // Selector の先頭に置いても技を途中で止めないのは、Selector が Running 中の子を先に再開
    // するから。技を出している間はこの枝まで評価が降りてこない。
    // 使わない場面（第2形態など）では、木からこのノードを外すか param を 0 にする。
    struct HitWaitAction : public IBTAction
    {
        BTNodeState Execute(BTContext &ctx) override
        {
            ctx.bt->lastLogActionId = A_BossHitWait;

            // 自分のタイマーを持たず、BossPerceptionSystem が書く値を param と比べるだけ。
            // 途中でもう一度被弾すれば、その分だけ延びる。
            return (ctx.bt->blackboard.timeSinceLastHit < ctx.param) ? BTNodeState::Running
                                                                     : BTNodeState::Failure;
        }
    };

    // 移動。currentActionId に A_Move を書き、0.5 秒で一度 Success にして BT に選び直させる。
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

            // 完了したら要求を取り下げる。消さないと BossActionSystem が最後の要求を
            // 実行し続け、次にボス本体の行動が選ばれるまでその動きのまま止まらない。
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

    // 詰めながら待つ。技と技の間に入れる待ち時間で、A_Wait の代わりに使う。
    // 技の後ろに A_Wait を置くと、中距離ではボスが一歩も動かず陣形だけを出し続ける
    // （A_Move は遠距離の枝にしか無く、上の枝が先に選ばれるため）。
    //
    // 近づき切っても Success にはせず、足だけを止めて param 秒の最後まで Running にする。
    // 途中で完了させると待ち時間がプレイヤーの位置で変わり、後隙が読めなくなる。
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
                cmd->currentActionId = A_Move; // 歩く（向き直りも ProcessMove が行う）
            else
                ClearCommandIfMine(ctx, A_Move); // 足を止める（待機の向き直りへ戻る）

            return BTNodeState::Running;
        }

        void OnAbort(BTContext &ctx) override { ClearCommandIfMine(ctx, A_Move); }
    };

    // ドローンの陣形の指示（2000番台）。陣形を要求して、すぐに完了する。
    // 予告や硬直の長さは持たず、技1回分は BT グラフのノードの並びで表す。
    //
    //     [陣形] [Wait 予告] [Strike] [Wait 攻撃＋硬直] [帰投] [Wait 次までの間]
    //
    // 時間の数字を陣形のクラス側にも持つと、数字の出どころが C++ と BT の2か所に分かれ、
    // 両方が攻撃信号を立てて同じ技が2回当たる。調整する数字は BT（エディタ）側だけに置く。
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

            DroneFormationType formation = DroneFormationType::None;
            if (id == A_DroneFormation)
            {
                // データの技は param の枠番号で指す。名前で指さないのは、エディタで名前を
                // 変えた瞬間に BT が技を見失うため（枠番号は技の識別子）。
                const int slot = static_cast<int>(std::lround(ctx.param));
                if (slot <= 0 || slot >= static_cast<int>(kDroneFormationSlotCount))
                    return BTNodeState::Failure;
                formation = static_cast<DroneFormationType>(slot);
            }
            // C++ で書いた陣形の対応は BT_DRONE_ACTION_LIST にだけ書く。
            else if (!TryGetDroneFormation(id, formation))
                return BTNodeState::Failure;

            auto *cmd = ctx.world->GetComponent<BossCommandComponent>(ctx.entity);
            if (!cmd) return BTNodeState::Failure;

            // 攻撃信号もここで下ろす。前の技の「今撃て」が残っていると、
            // 組み上がる途中の陣形がそのまま突っ込む。
            cmd->requestFormation  = formation;
            cmd->droneAttackActive = false;
            return BTNodeState::Success;
        }
    };

    // ドローンの攻撃開始（Strike）。「今撃て」信号を立て、DroneCommandSystem を通して
    // 各ドローンが FireCharge に入る。[陣形] の後ろにこれが無い枝は陣形を組んで戻るだけに
    // なる（BehaviorTreeLoader が読み込み時に警告を出す）。信号を下ろすのは次の陣形の指示。
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

    // アニメのトリガーで始める技。トリガーを引いて、param 秒の間 BT をこのノードに留める。
    // AI は技を決めてトリガーを引くだけで、当たり判定の時間はアニメクリップ内の
    // HitBox イベントが決め、体の動きは BossActionSystem が行う。
    //
    // 「近接攻撃」ではなく「トリガーで始める技」としているのは、後方ステップも同じ形だから。
    // 違いは triggerHash と BossActionSystem 側の動きだけ。triggerHash に対応する遷移がアニメグラフにあることが前提。
    struct TriggeredMotionAction : public IBTAction
    {
        uint32_t triggerHash; // 開始フレームに引くアニメのトリガー名のハッシュ

        float defaultDuration; // ノードの param が未設定(0)のときに使う技の全体時間 [秒]

        ActionID id; // この技の ActionID。currentActionId に書き、終わったら取り下げる
        TriggeredMotionAction(uint32_t hash, float fallbackDur, ActionID actionId)
            : triggerHash(hash), defaultDuration(fallbackDur), id(actionId) {}

        // 特に AnimParametersComponent の申告が必要。抜けていると、トリガーを消費する
        // AnimControllerSystem と並列に走り、同じ vector へ push_back と erase が同時に入る。
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

            // 開始フレームで、アニメのトリガーを1回だけ引く。
            if (timers[ctx.nodeId] == -1.0f)
            {
                timers[ctx.nodeId] = (ctx.param > 0.0f) ? ctx.param : defaultDuration;

                // ハッシュ0＝引くトリガーを持たない技。横歩きのように「続いている間ずっと
                // 流れるクリップ」は、開始の合図ではなく状態（StrafeDir）で絵を選ぶ。
                if (triggerHash != 0)
                    if (auto *anim = ctx.world->GetComponent<AnimParametersComponent>(ctx.entity))
                        anim->SetTrigger(triggerHash);
            }

            // どの技を出しているかと、その全体時間を BossActionSystem へ伝える。
            if (auto *cmd = ctx.world->GetComponent<BossCommandComponent>(ctx.entity))
            {
                cmd->currentActionId       = id;
                cmd->currentActionDuration = (ctx.param > 0.0f) ? ctx.param : defaultDuration;
            }

            return BTNodeState::Running;
        }

        // 空中にいる間は打ち切らせない。要求が下りると体を動かす側が待機へ倒れ、
        // 放物線の途中から重力で落ちる。着地した最初のフレームには許可が出る。
        bool CanAbort(BTContext &ctx) const override { return !IsAirborne(ctx); }

        void OnAbort(BTContext &ctx) override { ClearCommandIfMine(ctx, id); }
    };
    } // namespace

    void RegisterBossActions()
    {
        auto &reg = BTActionRegistry::Instance();

        // 各技の秒数は、BT がこの技に留まる全体時間（後隙を含む）。段ごとの内訳は
        // BossActionComponent 側で調整する。ノードに param があればそちらを使う。

        // 後方ステップ：後退0.35 + 硬直。攻撃判定は持たない。硬直をステップ本体より長く
        // 取るのは、0 だと「被弾→すぐ離脱→すぐ接近」を繰り返し、プレイヤーが攻撃する隙が無くなるため。
        reg.RegisterActionInstance(A_BossBackStep,
                                   std::make_unique<TriggeredMotionAction>(CCL::Utils::HashString("BackStep"), 0.9f, A_BossBackStep));

        // 中央へ跳んで逃げる：溜め0.25 + 滞空1.1 + 着地硬直（LeapLand クリップ 0.63秒）。
        // 全体時間が溜め＋滞空より短いと空中で要求が下りて待機へ戻り、重力で落ちる。
        // 跳躍系はどれも同じ "Leap" を引き、着地までのアニメの切り替えは IsLeaping（BossAnimSyncSystem）で行う。
        reg.RegisterActionInstance(A_BossLeapEscape,
                                   std::make_unique<TriggeredMotionAction>(CCL::Utils::HashString("Leap"), 1.9f, A_BossLeapEscape));

        // 跳躍攻撃：溜め0.35 + 滞空0.8 + 着地硬直。既定の全体時間は、攻撃判定を持つ着地
        // クリップ（Ultimate_Land・1.47秒）が終わるまでの長さにしてある。短くすると
        // 判定が出ている最中に次の技が始まる。
        // 攻撃の着地クリップへの切り替えは LeapAttack（BossAnimSyncSystem）で行うので、
        // 引くトリガーは逃げる跳躍と同じ。
        reg.RegisterActionInstance(A_BossLeapAttack,
                                   std::make_unique<TriggeredMotionAction>(CCL::Utils::HashString("Leap"), 2.6f, A_BossLeapAttack));

        // その場で滞空：溜め0.3 + 上昇下降0.9 + 頂点 + 着地硬直0.63（LeapLand）。
        // 頂点に留まる長さは全体時間の残り（BossActionSystem が逆算する）なので、ノードの
        // param を延ばせばそのまま頂点が延びる。既定の 4.4秒で頂点は約2.6秒。
        // ドローンの陣形攻撃は Parallel の従に置き、滞空（主）が全体の長さを決める。
        reg.RegisterActionInstance(A_BossAirHover,
                                   std::make_unique<TriggeredMotionAction>(CCL::Utils::HashString("Leap"), 4.4f, A_BossAirHover));

        // 突進攻撃：下がる0.5 + 狙う0.5 + 構える0.4 + 突っ込む（15m÷45m/s ≒ 0.33）+ 走る1.0
        // + 止まる0.4 + 硬直（残り）。全体時間が下がる＋狙う＋構える＋突っ込むより短いと、
        // 空中で要求が下りて待機へ戻り、重力で落ちる。走る・止まるの途中で切れた場合は、
        // その場で待機の減速に移る。
        // 横歩き：相手を見たまま回り込むだけの技。当たり判定も溜めも持たない。
        // 絵は状態（StrafeDir）で選ぶのでトリガーは引かない（ハッシュ0）。
        reg.RegisterActionInstance(A_BossStrafe,
                                   std::make_unique<TriggeredMotionAction>(0, 2.5f, A_BossStrafe));

        reg.RegisterActionInstance(A_BossDashAttack,
                                   std::make_unique<TriggeredMotionAction>(CCL::Utils::HashString("Dash"), 3.7f, A_BossDashAttack));

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
