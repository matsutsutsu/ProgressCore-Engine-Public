#include "Game/Logics/Character/Boss/Actions/BTActions_Boss.h"

#include "Game/Logics/AI/BehaviorTree/Actions/BTAction.h"
#include "Game/Logics/AI/BehaviorTree/Actions/BTActionRegistry.h"
#include "Game/Logics/AI/BehaviorTree/Data/BTActionIds.h"
#include "Game/Logics/Character/Boss/Components/BossCommandComponent.h"
#include "Game/Logics/Character/Boss/Components/BossActionComponent.h" // 詰めるのを止める距離・跳躍と突進の段
#include "Game/Logics/AI/Drone/Components/DroneComponent.h"
#include "Engine/ECS/Core/CCL_World.h"                           // world->GetComponent
#include "Engine/Animation/Components/AnimParametersComponent.h" // SetTrigger（技の開始）
#include "Engine/Core/Math/StringHash.h"                         // HashString

#include <cmath> // 枠番号は param（float）で来るので丸める

namespace CCL::AI::BT
{
    namespace
    {
        // ---------------------------------------------------------------------------
        // 共通の補助
        // ---------------------------------------------------------------------------

        // nodeTimers の特別な値。減らすのは BehaviorTreeSystem で、0 で止まる
        constexpr float kTimerNotStarted = -1.0f; // ノードに入った最初のフレーム
        constexpr float kTimerExpired    = 0.0f;  // 決めた時間を使い切った

        // 評価中のノードの残り時間。ノード数と合わなければ nullptr
        float *FindNodeTimer(BTContext &ctx)
        {
            auto &timers = ctx.bt->nodeTimers;
            if (ctx.nodeId >= timers.size())
            {
                return nullptr;
            }
            return &timers[ctx.nodeId];
        }

        // 自分が出した行動要求だけを取り下げる。完了・打ち切りのときに必ず呼ぶ。
        // 要求が残ると、BossActionSystem が最後の動きをそのまま続けてしまう。
        void ClearCommandIfMine(BTContext &ctx, ActionID expected)
        {
            auto *cmd = ctx.world->GetComponent<BossCommandComponent>(ctx.entity);
            if (!cmd)
            {
                return;
            }

            // 同じフレームに次の技が出した要求は消さない
            if (cmd->currentActionId == expected)
            {
                cmd->currentActionId = 0;
            }
        }

        // ボスの足が地面から離れているか。空中の技を途中で打ち切らせないために使う
        bool IsAirborne(BTContext &ctx)
        {
            const auto *act = ctx.world->GetComponent<BossActionComponent>(ctx.entity);
            if (!act)
            {
                return false;
            }

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
        struct WaitAction : public IBTAction
        {
            BTNodeState Execute(BTContext &ctx) override
            {
                float *timer = FindNodeTimer(ctx);
                if (!timer)
                {
                    return BTNodeState::Failure;
                }

                if (*timer == kTimerNotStarted)
                {
                    *timer = ctx.param;
                }

                return (*timer > 0.0f) ? BTNodeState::Running : BTNodeState::Success;
            }
        };

        // 蓄積ダメージを 0 に戻す
        struct ResetDamageAction : public IBTAction
        {
            BTNodeState Execute(BTContext &ctx) override
            {
                ctx.bt->blackboard.accumulatedDamage = 0.0f;
                return BTNodeState::Success;
            }
        };

        // ---------------------------------------------------------------------------
        // 移動と様子見
        // ---------------------------------------------------------------------------

        // 相手へ歩いて近づく。一定時間ごとに Success を返し、BT に次の行動を選び直させる
        struct MoveAction : public IBTAction
        {
            static constexpr float kReselectInterval = 0.5f; // 1回に歩く長さ [秒]。過ぎたら BT が選び直す

            std::vector<CCL::ECS::TypeID> GetExtraWriteTypes() const override
            {
                return TypeIDsOf<BossCommandComponent>();
            }

            BTNodeState Execute(BTContext &ctx) override
            {
                float *timer = FindNodeTimer(ctx);
                if (!timer)
                {
                    return BTNodeState::Failure;
                }

                ctx.bt->lastLogActionId = A_Move;

                if (*timer == kTimerExpired)
                {
                    ClearCommandIfMine(ctx, A_Move);
                    return BTNodeState::Success;
                }

                if (*timer == kTimerNotStarted)
                {
                    *timer = kReselectInterval;
                }

                if (auto *cmd = ctx.world->GetComponent<BossCommandComponent>(ctx.entity))
                {
                    cmd->currentActionId = A_Move;
                }
                return BTNodeState::Running;
            }

            // 割り込み先が要求を書かない技（様子見など）のとき、取り下げないとボスが歩き続ける
            void OnAbort(BTContext &ctx) override { ClearCommandIfMine(ctx, A_Move); }
        };

        // 詰めながら待つ。技と技の間の待ち時間で、A_Wait の代わりに使う。
        // A_Wait だと、中距離ではボスが一歩も動かず陣形だけを出し続ける（A_Move は遠距離の枝にしか無いため）。
        struct AdvanceAction : public IBTAction
        {
            static constexpr float kDefaultDuration      = 2.0f; // param が未設定(0)のときの待ち時間 [秒]
            static constexpr float kFallbackStopDistance = 7.0f; // BossActionComponent が無いときの足を止める距離 [m]

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
                float *timer = FindNodeTimer(ctx);
                if (!timer)
                {
                    return BTNodeState::Failure;
                }

                ctx.bt->lastLogActionId = A_BossAdvance;

                if (*timer == kTimerExpired)
                {
                    ClearCommandIfMine(ctx, A_Move);
                    return BTNodeState::Success;
                }

                if (*timer == kTimerNotStarted)
                {
                    *timer = (ctx.param > 0.0f) ? ctx.param : kDefaultDuration;
                }

                // --- 歩くか止まるか ---

                float stopDistance = kFallbackStopDistance;
                if (const auto *act = ctx.world->GetComponent<BossActionComponent>(ctx.entity))
                {
                    stopDistance = act->advanceStopDistance;
                }

                auto *cmd = ctx.world->GetComponent<BossCommandComponent>(ctx.entity);
                if (!cmd)
                {
                    return BTNodeState::Running;
                }

                // 近づき切っても Success にせず、待ち時間の最後まで Running にする。
                // 途中で完了させると待ち時間がプレイヤーの位置で変わり、後隙が読めなくなる。
                if (ctx.bt->blackboard.distanceToTarget > stopDistance)
                {
                    cmd->currentActionId = A_Move;
                }
                else
                {
                    ClearCommandIfMine(ctx, A_Move);
                }

                return BTNodeState::Running;
            }

            void OnAbort(BTContext &ctx) override { ClearCommandIfMine(ctx, A_Move); }
        };

        // 被弾後の様子見。最後の被弾から param 秒たつまで Running、たったら Failure。
        // Selector は Running 中の子を先に再開するので、先頭に置いても出している技は止めない。
        struct HitWaitAction : public IBTAction
        {
            BTNodeState Execute(BTContext &ctx) override
            {
                ctx.bt->lastLogActionId = A_BossHitWait;

                // タイマーは持たず、BossPerceptionSystem が書く経過時間と比べる（再被弾で延びる）
                return (ctx.bt->blackboard.timeSinceLastHit < ctx.param) ? BTNodeState::Running
                                                                         : BTNodeState::Failure;
            }
        };

        // ---------------------------------------------------------------------------
        // トリガーで始める技（後方ステップ・跳躍・突進・横歩き）
        // ---------------------------------------------------------------------------

        // アニメのトリガーを引き、技の全体時間のあいだ BT をこのノードに留める。
        // 体の動きは BossActionSystem が、攻撃判定の時間はアニメクリップの HitBox イベントが決める。
        struct TriggeredMotionAction : public IBTAction
        {
            ActionID id;              // この技の ActionID。currentActionId に書き、終わったら取り下げる
            uint32_t triggerHash;     // 開始フレームに引くアニメのトリガー名のハッシュ。0 は引かない
            float    defaultDuration; // param が未設定(0)のときの技の全体時間 [秒]

            TriggeredMotionAction(ActionID actionId, uint32_t hash, float fallbackDuration)
                : id(actionId), triggerHash(hash), defaultDuration(fallbackDuration) {}

            std::vector<CCL::ECS::TypeID> GetExtraWriteTypes() const override
            {
                return TypeIDsOf<BossCommandComponent, AnimParametersComponent>();
            }

            std::vector<CCL::ECS::TypeID> GetExtraReadTypes() const override
            {
                return TypeIDsOf<BossActionComponent>();
            }

            BTNodeState Execute(BTContext &ctx) override
            {
                float *timer = FindNodeTimer(ctx);
                if (!timer)
                {
                    return BTNodeState::Failure;
                }

                ctx.bt->lastLogActionId = id;

                // --- 完了（硬直まで終わった）---

                if (*timer == kTimerExpired)
                {
                    ClearCommandIfMine(ctx, id);
                    return BTNodeState::Success;
                }

                // --- 開始 ---

                if (*timer == kTimerNotStarted)
                {
                    *timer = Duration(ctx);

                    // 横歩きはトリガーを持たず、状態（StrafeDir）でクリップを選ぶ
                    if (triggerHash != 0)
                    {
                        if (auto *anim = ctx.world->GetComponent<AnimParametersComponent>(ctx.entity))
                        {
                            anim->SetTrigger(triggerHash);
                        }
                    }
                }

                // --- BossActionSystem へ伝える ---

                // 全体時間も渡す。BossActionSystem はどれか1つの段の長さを残りから逆算する
                if (auto *cmd = ctx.world->GetComponent<BossCommandComponent>(ctx.entity))
                {
                    cmd->currentActionId       = id;
                    cmd->currentActionDuration = Duration(ctx);
                }

                return BTNodeState::Running;
            }

            // 空中では打ち切らせない。要求が下りると BossActionSystem が待機へ倒れ、放物線の途中から落ちる
            bool CanAbort(BTContext &ctx) const override { return !IsAirborne(ctx); }

            void OnAbort(BTContext &ctx) override { ClearCommandIfMine(ctx, id); }

            // この技の全体時間 [秒]。ノードの param があればそちらを使う
            float Duration(const BTContext &ctx) const { return (ctx.param > 0.0f) ? ctx.param : defaultDuration; }
        };

        // ---------------------------------------------------------------------------
        // ドローンへの指示（陣形・攻撃開始）
        // ---------------------------------------------------------------------------

        // ドローンへ陣形を指示して、すぐに完了する。
        // 予告や硬直の長さは持たず、BT のノードの並びで表す（数字を C++ と BT の2か所に持たないため）:
        //     [陣形] [Wait 予告] [Strike] [Wait 攻撃＋硬直] [帰投] [Wait 次までの間]
        struct DroneAction : public IBTAction
        {
            ActionID id; // どの陣形指示か（2000番台）

            explicit DroneAction(ActionID actionId) : id(actionId) {}

            std::vector<CCL::ECS::TypeID> GetExtraWriteTypes() const override
            {
                return TypeIDsOf<BossCommandComponent>();
            }

            BTNodeState Execute(BTContext &ctx) override
            {
                ctx.bt->lastLogActionId = id;

                DroneFormationType formation = DroneFormationType::None;
                if (!TryResolveFormation(ctx, formation))
                {
                    return BTNodeState::Failure;
                }

                auto *cmd = ctx.world->GetComponent<BossCommandComponent>(ctx.entity);
                if (!cmd)
                {
                    return BTNodeState::Failure;
                }

                // 攻撃信号もここで下ろす。前の技の「今撃て」が残ると、組み上がる途中の陣形が突っ込む
                cmd->requestFormation  = formation;
                cmd->droneAttackActive = false;
                return BTNodeState::Success;
            }

            // このノードが指す陣形を求める。枠番号が範囲外・対応の無い ActionID なら false
            bool TryResolveFormation(const BTContext &ctx, DroneFormationType &out) const
            {
                // データの技は param の枠番号で指す（名前だと、エディタで変えた瞬間に見失う）
                if (id == A_DroneFormation)
                {
                    const int slot = static_cast<int>(std::lround(ctx.param));
                    if (slot <= 0 || slot >= static_cast<int>(kDroneFormationSlotCount))
                    {
                        return false;
                    }
                    out = static_cast<DroneFormationType>(slot);
                    return true;
                }

                // C++ で書いた陣形の対応は BT_DRONE_ACTION_LIST にだけ書く
                return TryGetDroneFormation(id, out);
            }
        };

        // ドローンの攻撃開始。「今撃て」信号を立て、下ろすのは次の陣形の指示（DroneAction）。
        // [陣形] の後ろにこれが無い枝は、陣形を組んで戻るだけになる（BehaviorTreeLoader が警告を出す）。
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

        // --- 移動と様子見 ---

        reg.RegisterAction<MoveAction>(A_Move);
        reg.RegisterAction<AdvanceAction>(A_BossAdvance);
        reg.RegisterAction<HitWaitAction>(A_BossHitWait);

        // --- トリガーで始める技 ---
        //
        // 秒数は、ノードの param が未設定のときの技の全体時間（後隙を含む）。
        // 段ごとの長さは BossActionComponent が持ち、どれか1つの段を全体時間の残りから逆算する。

        // 後方ステップ。全体時間の残りが着地硬直。
        // 硬直が無いと「被弾→離脱→接近」を繰り返し、プレイヤーが攻撃する隙が無くなる。
        reg.RegisterActionInstance(A_BossBackStep,
                                   std::make_unique<TriggeredMotionAction>(A_BossBackStep, CCL::Utils::HashString("BackStep"), 0.9f));

        // 跳躍3種は同じ "Leap" を引く。着地のクリップは BossAnimSyncSystem の IsLeaping / LeapAttack で切り替える
        const uint32_t leapTrigger = CCL::Utils::HashString("Leap");

        // 逃げる・跳躍攻撃：空中の長さ＝全体時間−溜め−着地硬直（延ばすと同じ距離をゆっくり飛ぶ）
        reg.RegisterActionInstance(A_BossLeapEscape,
                                   std::make_unique<TriggeredMotionAction>(A_BossLeapEscape, leapTrigger, 1.9f));
        reg.RegisterActionInstance(A_BossLeapAttack,
                                   std::make_unique<TriggeredMotionAction>(A_BossLeapAttack, leapTrigger, 2.6f));

        // 滞空：頂点に留まる長さ＝全体時間−溜め−上昇下降−着地硬直。
        // ドローンの陣形攻撃は Parallel の従に置き、滞空（主）が全体の長さを決める。
        reg.RegisterActionInstance(A_BossAirHover,
                                   std::make_unique<TriggeredMotionAction>(A_BossAirHover, leapTrigger, 4.4f));

        // 突進：着地後に走り続ける長さが全体時間の残り。
        // 全体時間が「下がる＋狙う＋構える＋突っ込む」より短いと、空中で要求が下りて落ちる。
        reg.RegisterActionInstance(A_BossDashAttack,
                                   std::make_unique<TriggeredMotionAction>(A_BossDashAttack, CCL::Utils::HashString("Dash"), 3.7f));

        // 横歩き：当たり判定も溜めも持たない。クリップは状態で選ぶのでトリガーは引かない（0）
        reg.RegisterActionInstance(A_BossStrafe,
                                   std::make_unique<TriggeredMotionAction>(A_BossStrafe, 0, 2.5f));

        // --- ドローンへの指示 ---

        reg.RegisterAction<DroneStrikeAction>(A_DroneStrike);

        // データの技はこの1つ。どの技かは枠番号（param）が決める
        reg.RegisterActionInstance(A_DroneFormation, std::make_unique<DroneAction>(A_DroneFormation));

        // C++ で書いた陣形は表から直接登録する（連番ループだと欠番も登録してしまう）
#define CCL_REGISTER_DRONE_ACTION(Name, Id, Formation, Role, Display) reg.RegisterActionInstance(Name, std::make_unique<DroneAction>(Name));
        BT_DRONE_ACTION_LIST(CCL_REGISTER_DRONE_ACTION)
#undef CCL_REGISTER_DRONE_ACTION
    }
} // namespace CCL::AI::BT
