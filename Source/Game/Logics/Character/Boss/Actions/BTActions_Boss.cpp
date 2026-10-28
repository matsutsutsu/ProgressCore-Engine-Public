#include "Game/Logics/Character/Boss/Actions/BTActions_Boss.h"

#include "Game/Logics/AI/BehaviorTree/Actions/BTAction.h"
#include "Game/Logics/AI/BehaviorTree/Actions/BTActionRegistry.h"
#include "Game/Logics/AI/BehaviorTree/Data/BTActionIds.h"
#include "Game/Logics/Character/Boss/Components/BossCommandComponent.h"
#include "Game/Logics/Character/Boss/Components/BossActionComponent.h" // 跳躍と突進の段
#include "Game/Logics/AI/Drone/Components/DroneComponent.h"
#include "Engine/ECS/Core/CCL_World.h" // world->GetComponent

#include <algorithm> // 待機の秒数を 0 以上にそろえる
#include <cassert>
#include <cmath> // 枠番号は param（float）で来るので丸める

namespace CCL::AI::BT
{
    namespace
    {
        // ---------------------------------------------------------------------------
        // 共通の補助
        // ---------------------------------------------------------------------------

        // nodeTimers の特別な値。
        // 減らすのは BehaviorTreeSystem で、0 で止まる
        constexpr float kTimerNotStarted = -1.0f; // ノードに入った最初のフレーム
        constexpr float kTimerFinished   = 0.0f;  // 決めた時間を使い切った

        // 評価中のノードの残り時間。
        // 配列がノード数と合わない木は
        // BehaviorTreeSystem::Update が評価しないので、
        // 範囲外にはならない
        float &NodeTimer(BTContext &ctx)
        {
            assert(ctx.nodeId < ctx.bt->nodeTimers.size() && "nodeTimers がノード数と合っていない");
            return ctx.bt->nodeTimers[ctx.nodeId];
        }

        // ボスの足が地面から離れているか。
        // 空中の技を途中で打ち切らせないために使う
        bool IsAirborne(BTContext &ctx)
        {
            const auto *act = ctx.world->GetComponent<BossActionComponent>(ctx.entity);
            if (!act) return false;

            // --- 跳躍 ---

            // 溜め（Windup）の間はまだ地面にいる
            if (act->currentState == BossActionState::Leap)
            {
                return act->leapPhase != BossLeapPhase::Windup;
            }

            // --- 突進 ---

            if (act->currentState == BossActionState::Dash)
            {
                switch (act->dashPhase)
                {
                case BossDashPhase::Retreat:
                case BossDashPhase::Aim:
                case BossDashPhase::Windup:
                case BossDashPhase::Charge:
                    return true; // 下がり始めから、突っ込み終わって地面に着くまで
                default:
                    return false; // Travel 以降は地面を走っているだけ
                }
            }

            return false;
        }

        // ---------------------------------------------------------------------------
        // 汎用の行動（待機・蓄積ダメージ）
        // ---------------------------------------------------------------------------

        // 待機。param 秒のあいだ Running を返す
        class WaitAction : public IBTAction
        {
        public:
            BTNodeState Execute(BTContext &ctx) override
            {
                float &timer = NodeTimer(ctx);

                // 入った最初のフレームに待つ秒数を入れ、
                // 使い切ったら完了する（減らすのは BehaviorTreeSystem）。
                // 負の秒数は 0 にそろえる。
                // そのままだと 0 にも -1 にもならず、時間切れを判定できない
                if (timer == kTimerNotStarted)
                {
                    timer = (std::max)(0.0f, ctx.param);
                }
                if (timer == kTimerFinished) return BTNodeState::Success;

                return BTNodeState::Running;
            }
        };

        // 蓄積ダメージを 0 に戻す
        class ResetDamageAction : public IBTAction
        {
        public:
            BTNodeState Execute(BTContext &ctx) override
            {
                ctx.bt->blackboard.accumulatedDamage = 0.0f;
                return BTNodeState::Success;
            }
        };

        // ---------------------------------------------------------------------------
        // 移動
        // ---------------------------------------------------------------------------

        // 相手へ歩いて近づく。
        // 一定時間ごとに Success を返し、BT に次の行動を選び直させる
        class MoveAction : public IBTAction
        {
        public:
            // 1回に歩く長さ [秒]。過ぎたら BT が選び直す
            static constexpr float kReselectInterval = 0.5f;

            std::vector<CCL::ECS::TypeID> GetExtraWriteTypes() const override
            {
                return TypeIDsOf<BossCommandComponent>();
            }

            BTNodeState Execute(BTContext &ctx) override
            {
                float &timer = NodeTimer(ctx);

                ctx.bt->lastLogActionId = A_Move; // BT デバッガに出す行動名

                // 入った最初のフレームに歩く長さを入れ、
                // 使い切ったら完了する
                if (timer == kTimerNotStarted) timer = kReselectInterval;
                if (timer == kTimerFinished) return BTNodeState::Success;

                // 歩いている間は、毎フレーム
                // ボス本体へ移動を要求する
                if (auto *cmd = ctx.world->GetComponent<BossCommandComponent>(ctx.entity))
                {
                    cmd->currentActionId = A_Move;
                }
                return BTNodeState::Running;
            }
        };

        // ---------------------------------------------------------------------------
        // 体を動かす技（後方ステップ・跳躍・突進・横歩き）
        // ---------------------------------------------------------------------------

        // ボス本体へ技を要求し、技が硬直まで終わったら完了する。
        // 技の長さは BossActionComponent の段ごとの時間が決め、BT は持たない。
        // 攻撃判定の時間はアニメクリップの HitBox イベントが決める。
        class MotionAction : public IBTAction
        {
        public:
            // 技が終わらないときの上限 [秒]。過ぎたら完了にする
            // 本体が技を進めない間（演出中など）に、BT がここで止まり続けないため
            static constexpr float kTimeLimit = 30.0f;

            ActionID id; // この技の ActionID

            explicit MotionAction(ActionID actionId) : id(actionId) {}

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
                float &timer = NodeTimer(ctx);

                ctx.bt->lastLogActionId = id; // BT デバッガに出す行動名

                auto *cmd = ctx.world->GetComponent<BossCommandComponent>(ctx.entity);
                if (!cmd) return BTNodeState::Failure;

                // 入った最初のフレームに、技の依頼番号を1つ進める
                // 本体はこの番号の変化で、同じ技の続けての依頼も新しい技として始める
                if (timer == kTimerNotStarted)
                {
                    timer = kTimeLimit;
                    ++cmd->actionSerial;
                }
                if (timer == kTimerFinished) return BTNodeState::Success;

                // 本体がこの依頼の技を硬直まで終えたら完了する
                // 番号を比べるのは、前の技の「終わった」を見て即完了しないため
                const auto *act = ctx.world->GetComponent<BossActionComponent>(ctx.entity);
                if (act && act->actionSerial == cmd->actionSerial && act->actionFinished)
                    return BTNodeState::Success;

                // 技が続いている間は、毎フレーム本体へ技を要求する
                cmd->currentActionId = id;
                return BTNodeState::Running;
            }

            // 別の枝が割り込もうとしたとき、打ち切ってよいか。
            // BehaviorTreeSystem::CanAbortSubtree が基底クラス経由で呼ぶ。
            // 空中では打ち切らせない。
            // 要求が書かれなくなると BossActionSystem が待機へ倒れ、
            // 放物線の途中から落ちる
            bool CanAbort(BTContext &ctx) const override { return !IsAirborne(ctx); }
        };

        // 横歩き。param 秒のあいだ、ボス本体へ横歩きを要求する
        // 横歩きには終わりの形が無いので、長さは BT が決める
        class StrafeAction : public IBTAction
        {
        public:
            std::vector<CCL::ECS::TypeID> GetExtraWriteTypes() const override
            {
                return TypeIDsOf<BossCommandComponent>();
            }

            BTNodeState Execute(BTContext &ctx) override
            {
                float &timer = NodeTimer(ctx);

                ctx.bt->lastLogActionId = A_BossStrafe; // BT デバッガに出す行動名

                // 歩く時間が未設定（0以下）のノードは、歩かずに失敗する
                // 負の値のまま始めると、時間切れにならず終わらない
                if (ctx.param <= 0.0f) return BTNodeState::Failure;

                // 入った最初のフレームに歩く時間を入れ、使い切ったら完了する
                if (timer == kTimerNotStarted) timer = ctx.param;
                if (timer == kTimerFinished) return BTNodeState::Success;

                // 歩いている間は、毎フレームボス本体へ横歩きを要求する
                if (auto *cmd = ctx.world->GetComponent<BossCommandComponent>(ctx.entity))
                {
                    cmd->currentActionId = A_BossStrafe;
                }
                return BTNodeState::Running;
            }
        };

        // ---------------------------------------------------------------------------
        // ドローンへの指示（陣形・攻撃開始）
        // ---------------------------------------------------------------------------

        // ドローンへ陣形を指示して、すぐに完了する。
        // 予告や硬直の長さは持たず、BT のノードの並びで表す
        // （数字を C++ と BT の2か所に持たないため）:
        //     [陣形] [Wait 予告] [Strike] [Wait 攻撃＋硬直] [帰投] [Wait 次までの間]
        class DroneAction : public IBTAction
        {
        public:
            // どの陣形指示か。C++ の陣形技（A_DroneHighOrbit など）か、
            // データの技（A_DroneFormation）のどちらか
            ActionID id;

            explicit DroneAction(ActionID actionId) : id(actionId) {}

            std::vector<CCL::ECS::TypeID> GetExtraWriteTypes() const override
            {
                return TypeIDsOf<BossCommandComponent>();
            }

            BTNodeState Execute(BTContext &ctx) override
            {
                ctx.bt->lastLogActionId = id; // BT デバッガに出す行動名

                // このノードが指す陣形を引く
                // （C++ の陣形は ActionID から、データの陣形は param の枠番号から）。
                // 対応する陣形が無ければ、指示を出さずに失敗する
                DroneFormationType formation = DroneFormationType::None;
                if (!TryResolveFormation(ctx, formation)) return BTNodeState::Failure;

                auto *cmd = ctx.world->GetComponent<BossCommandComponent>(ctx.entity);
                if (!cmd) return BTNodeState::Failure;

                // 陣形を指示する。
                // 読むのは DroneCommandSystem で、各機の持ち場へ配る。
                // 攻撃信号もここで下ろす。前の技の「今撃て」が残ると、
                // 組み上がる途中の陣形が突っ込む
                cmd->requestFormation  = formation;
                cmd->droneAttackActive = false;
                return BTNodeState::Success;
            }

            // このノードが指す陣形を out に書いて true を返す。
            // データの技は param の枠番号から、
            // C++ の陣形技は ActionID から引く。
            // 引けなければ false（out はそのまま）
            bool TryResolveFormation(const BTContext &ctx, DroneFormationType &out) const
            {
                // データ駆動の陣形の場合は param が枠番号になる
                if (id == A_DroneFormation)
                {
                    // param は float で来るので丸める。
                    // 0 は None、1～48 が陣形の枠番号
                    const int slot = static_cast<int>(std::lround(ctx.param));
                    if (slot <= 0 || slot >= static_cast<int>(kDroneFormationSlotCount))
                    {
                        return false;
                    }
                    out = static_cast<DroneFormationType>(slot);
                    return true;
                }

                // C++ で書いた陣形技は、ActionID → 陣形の対応表を引く。
                // 表に無い ID なら false。
                // 対応表は BT_DRONE_ACTION_LIST の1か所だけに書く
                return TryGetDroneFormation(id, out);
            }
        };

        // ドローンの攻撃開始。「今撃て」信号を立て、
        // 下ろすのは次の陣形の指示（DroneAction）。
        // [陣形] の後ろにこれが無い枝は、陣形を組んで戻るだけになる
        class DroneStrikeAction : public IBTAction
        {
        public:
            std::vector<CCL::ECS::TypeID> GetExtraWriteTypes() const override
            {
                return TypeIDsOf<BossCommandComponent>();
            }

            BTNodeState Execute(BTContext &ctx) override
            {
                ctx.bt->lastLogActionId = A_DroneStrike; // BT デバッガに出す行動名

                if (auto *cmd = ctx.world->GetComponent<BossCommandComponent>(ctx.entity))
                {
                    cmd->droneAttackActive = true;
                }
                return BTNodeState::Success;
            }
        };
    } // namespace

    // ---------------------------------------------------------------------------
    // 登録
    // ---------------------------------------------------------------------------

    void RegisterBossActions()
    {
        auto &reg = BTActionRegistry::Instance();

        // --- 汎用の行動 ---

        reg.RegisterAction<WaitAction>(A_Wait);
        reg.RegisterAction<ResetDamageAction>(A_ResetDamage);

        // --- 移動 ---

        reg.RegisterAction<MoveAction>(A_Move);

        // --- 体を動かす技 ---

        reg.RegisterActionInstance(A_BossBackStep, std::make_unique<MotionAction>(A_BossBackStep));
        reg.RegisterActionInstance(A_BossLeapEscape, std::make_unique<MotionAction>(A_BossLeapEscape));
        reg.RegisterActionInstance(A_BossLeapAttack, std::make_unique<MotionAction>(A_BossLeapAttack));
        reg.RegisterActionInstance(A_BossAirHover, std::make_unique<MotionAction>(A_BossAirHover));
        reg.RegisterActionInstance(A_BossDashAttack, std::make_unique<MotionAction>(A_BossDashAttack));
        reg.RegisterAction<StrafeAction>(A_BossStrafe);

        // --- ドローンへの指示 ---

        reg.RegisterAction<DroneStrikeAction>(A_DroneStrike);

        // データの技はこの1つ。どの技かは枠番号（param）が決める
        reg.RegisterActionInstance(A_DroneFormation, std::make_unique<DroneAction>(A_DroneFormation));

        // C++ で書いた陣形は表から直接登録する
        // （連番ループだと欠番も登録してしまう）。
        // 表の1行ごとに
        // 「reg.RegisterActionInstance(名前, DroneAction(名前));」が並ぶ
#define CCL_REGISTER_DRONE_ACTION(Name, Id, Formation, Role, Display) reg.RegisterActionInstance(Name, std::make_unique<DroneAction>(Name));
        BT_DRONE_ACTION_LIST(CCL_REGISTER_DRONE_ACTION)
#undef CCL_REGISTER_DRONE_ACTION
    }
} // namespace CCL::AI::BT
