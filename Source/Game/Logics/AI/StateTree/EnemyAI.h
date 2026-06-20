/// @file  EnemyAI.h
/// @brief アップロードされた EnemyComponent を StateTree で動かすための
///        タスク・条件・アセット定義（M1: ハードコード版）。
///
/// 設計の要点:
///   - ステータス（moveSpeed / attackRange / attackDamage）は EnemyComponent から読む。
///     → アセットは「行動の形」、EnemyComponent は「個体の数値」を担い、責務が分かれる。
///   - 評価は選択駆動。最も具体的な条件を満たす状態が自動で選ばれる:
///       近接圏内なら Attack / 索敵圏内なら Chase / それ以外は Idle
///
/// 状態ツリー:
///   [0] Root
///     [1] Attack  enterCond: 攻撃射程内 (enemy.attackRange)   task: MeleeAttack
///     [2] Chase   enterCond: 索敵範囲内 (detectRange)         task: MoveToTarget
///     [3] Idle    enterCond: なし(フォールバック)             task: Idle
///   ※ 子の順序が優先順位。Attack を先頭に置くことで「近ければ攻撃」が最優先になる。
#pragma once
#include "StateTreeAsset.h"
#include "StateTreeNode.h"
#include "NodeRegistry.h"

#include "Game/Logics/Character/Enemy/EnemyComponent.h"
#include "PerceptionComponent.h"
#include "MoveIntentComponent.h"
#include "AttackIntentComponent.h"

#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/Platform/Core/Logger.h"


namespace EnemyAI
{
    // ====================================================================
    // タスク
    // ====================================================================

    /// @brief 待機。移動意図をゼロにしてその場で止まる。
    struct IdleTask : public CCL::AI::ITask
    {
        struct Instance { float elapsed = 0.0f; };
        uint32_t InstanceDataSize() const override { return sizeof(Instance); }

        CCL::AI::RunStatus Tick(CCL::AI::ExecContext& ctx) override
        {
            auto* inst = static_cast<Instance*>(ctx.instance);
            if (inst) inst->elapsed += ctx.dt;

            if (auto* mi = ctx.world->GetComponent<MoveIntentComponent>(ctx.entity)) {
                mi->hasTarget = false;
                mi->desiredSpeed = 0.0f;
            }
            return CCL::AI::RunStatus::Running;
        }
    };

    /// @brief プレイヤーへ接近。EnemyComponent.moveSpeed で移動意図を出す。
    struct MoveToTargetTask : public CCL::AI::ITask
    {
        CCL::AI::RunStatus Tick(CCL::AI::ExecContext& ctx) override
        {
            auto* perc = ctx.world->GetComponent<PerceptionComponent>(ctx.entity);
            auto* enemy = ctx.world->GetComponent<EnemyComponent>(ctx.entity);
            auto* mi = ctx.world->GetComponent<MoveIntentComponent>(ctx.entity);
            if (!perc || !enemy || !mi) return CCL::AI::RunStatus::Failed;

            // 意図を書く（実移動は EnemyMovementSystem が行う）
            mi->hasTarget = true;
            mi->desiredTarget = perc->lastKnownPos;
            mi->desiredSpeed = enemy->moveSpeed;   // ★ EnemyComponent から
            return CCL::AI::RunStatus::Running;
        }

        void ExitState(CCL::AI::ExecContext& ctx) override
        {
            if (auto* mi = ctx.world->GetComponent<MoveIntentComponent>(ctx.entity)) {
                mi->desiredSpeed = 0.0f;
            }
        }
    };

    /// @brief 近接攻撃。プレイヤーを向きつつ、cooldown 周期でダメージ意図を発行。
    struct MeleeAttackTask : public CCL::AI::ITask
    {
        struct Params { float animDuration = 0.6f; float cooldown = 1.2f; };
        struct Instance { float elapsed = 0.0f; bool hasFired = false; };

        uint32_t InstanceDataSize() const override { return sizeof(Instance); }

        CCL::AI::RunStatus Tick(CCL::AI::ExecContext& ctx) override
        {
            const auto* p = static_cast<const Params*>(ctx.params);
            auto* inst = static_cast<Instance*>(ctx.instance);
            auto* perc = ctx.world->GetComponent<PerceptionComponent>(ctx.entity);
            auto* enemy = ctx.world->GetComponent<EnemyComponent>(ctx.entity);
            auto* mi = ctx.world->GetComponent<MoveIntentComponent>(ctx.entity);
            auto* ai = ctx.world->GetComponent<AttackIntentComponent>(ctx.entity);
            if (!inst || !p || !enemy || !ai) return CCL::AI::RunStatus::Failed;

            // プレイヤーを向く（移動はしない：speed=0、回転だけ Movement に任せる）
            if (mi && perc) {
                mi->hasTarget = true;
                mi->desiredTarget = perc->lastKnownPos;
                mi->desiredSpeed = 0.0f;
            }

            inst->elapsed += ctx.dt;

            // モーション中間でダメージ意図を1回発行
            if (!inst->hasFired && inst->elapsed >= p->animDuration * 0.5f) {
                ai->requested = true;
                ai->damage = enemy->attackDamage;   // ★ EnemyComponent から
                inst->hasFired = true;
                CCL_LOG_INFO(LogCategory::Game, "[EnemyAI] Attack! dmg=%.1f", ai->damage);
            }
            else {
                ai->requested = false;
            }

            // cooldown 経過で1周リセット（滞在し続ける限り連続攻撃）
            if (inst->elapsed >= p->cooldown) {
                inst->elapsed = 0.0f;
                inst->hasFired = false;
            }
            return CCL::AI::RunStatus::Running;
        }

        void ExitState(CCL::AI::ExecContext& ctx) override
        {
            if (auto* ai = ctx.world->GetComponent<AttackIntentComponent>(ctx.entity))
                ai->requested = false;
        }

        static const std::vector<CCL::AI::NodeParamField>& ParamFields()
        {
            static const std::vector<CCL::AI::NodeParamField> f = {
                NODE_PARAM_FLOAT(Params, animDuration, "Anim Duration", 0.6f, 0.0f, 5.0f),
                NODE_PARAM_FLOAT(Params, cooldown,     "Cooldown",      1.2f, 0.0f, 10.0f),
            };
            return f;
        }

    };


    // ====================================================================
    // 条件
    // ====================================================================

    /// @brief 索敵範囲（detectRange）内にプレイヤーがいるか。範囲はパラメータ。
    struct IsTargetInRangeCondition : public CCL::AI::ICondition
    {
        struct Params { float range = 12.0f; };

        static const std::vector<CCL::AI::NodeParamField>& ParamFields()
        {
            static const std::vector<CCL::AI::NodeParamField> f = {
                NODE_PARAM_FLOAT(Params, range, "Detect Range", 12.0f, 0.0f, 50.0f),
            };
            return f;
        }

        bool Evaluate(CCL::AI::ExecContext& ctx) override
        {
            const auto* p = static_cast<const Params*>(ctx.params);
            const auto* perc = ctx.world->GetComponent<PerceptionComponent>(ctx.entity);
            if (!p || !perc) return false;
            return perc->target != CCL::ECS::InvalidEntityID
                && perc->distance <= p->range;
        }


    };

    /// @brief 攻撃射程内にプレイヤーがいるか。射程は EnemyComponent.attackRange。
    struct IsInAttackRangeCondition : public CCL::AI::ICondition
    {
        bool Evaluate(CCL::AI::ExecContext& ctx) override
        {
            const auto* perc = ctx.world->GetComponent<PerceptionComponent>(ctx.entity);
            const auto* enemy = ctx.world->GetComponent<EnemyComponent>(ctx.entity);
            if (!perc || !enemy) return false;
            return perc->target != CCL::ECS::InvalidEntityID
                && perc->distance <= enemy->attackRange;   // ★ EnemyComponent から
        }
    };


    // ====================================================================
    // 登録 & アセット組み立て
    // ====================================================================

    struct RegisteredIDs
    {
        CCL::AI::NodeTypeID idleTask, moveTask, attackTask;
        CCL::AI::NodeTypeID inDetectRange, inAttackRange;
    };

    /// @brief ノード種別を NodeRegistry に登録。Scene 初期化で1回だけ呼ぶ。
    inline RegisteredIDs RegisterNodes()
    {
        auto& reg = CCL::AI::NodeRegistry::Instance();
        RegisteredIDs ids;
        ids.idleTask = reg.RegisterTask<IdleTask>("Idle", "Enemy");
        ids.moveTask = reg.RegisterTask<MoveToTargetTask>("MoveToTarget", "Movement");
        ids.attackTask = reg.RegisterTask<MeleeAttackTask>("MeleeAttack", "Combat");
        ids.inDetectRange = reg.RegisterCondition<IsTargetInRangeCondition>("IsTargetInRange", "Perception");
        ids.inAttackRange = reg.RegisterCondition<IsInAttackRangeCondition>("IsInAttackRange", "Perception");
        return ids;
    }

    /// @brief 標準的な敵の StateTreeAsset をハードコードで組み立てる。
    /// @param detectRange 索敵範囲（メートル）。Chase 入場の閾値。
    inline CCL::AI::StateTreeAsset BuildEnemyAsset(const RegisteredIDs& ids,
        float detectRange = 12.0f)
    {
        using namespace CCL::AI;
        StateTreeAsset asset;
        asset.name = "Enemy";

        const uint16_t root = asset.AddState("Root");

        // --- Attack（最優先・先頭）---
        const uint16_t attack = asset.AddState("Attack", root);
        {
            asset.AddEnterCondition(attack, ids.inAttackRange, 0, 0); // パラメータ無し
            MeleeAttackTask::Params tp{ .animDuration = 0.6f, .cooldown = 1.2f };
            asset.AddTask(attack, ids.attackTask,
                asset.AddParams(tp), sizeof(tp),
                sizeof(MeleeAttackTask::Instance));
        }

        // --- Chase ---
        const uint16_t chase = asset.AddState("Chase", root);
        {
            IsTargetInRangeCondition::Params cp{ .range = detectRange };
            asset.AddEnterCondition(chase, ids.inDetectRange,
                asset.AddParams(cp), sizeof(cp));
            asset.AddTask(chase, ids.moveTask, 0, 0); // パラメータ無し（速度は EnemyComponent）
        }

        // --- Idle（フォールバック・末尾・入場条件なし）---
        const uint16_t idle = asset.AddState("Idle", root);
        {
            asset.AddTask(idle, ids.idleTask, 0, 0, sizeof(IdleTask::Instance));
        }

        (void)root;
        return asset;
    }

} // namespace EnemyAI