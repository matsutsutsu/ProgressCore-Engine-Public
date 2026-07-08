/// @file  DemoAI.h
#pragma once
#include "Game/Logics/AI/StateTree/Core/StateTreeAsset.h"
#include "Game/Logics/AI/StateTree/Core/StateTreeNode.h"
#include "Game/Logics/AI/StateTree/Core/NodeRegistry.h"
#include "Game/Logics/AI/StateTree/Core/NodeParamField.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/Timer/TimeState.h"
#include "Engine/Platform/Core/Logger.h"
#include <SimpleMath.h>

// 実際の TransformComponent のヘッダーパスに合わせてください
#include "Engine/Transform/Components/TransformComponent.h"

namespace DemoAI
{
struct DemoIdleTask : public CCL::AI::ITask
{
    struct Instance
    {
        float timer = 0.0f;
    };
    uint32_t InstanceDataSize() const override
    {
        return sizeof(Instance);
    }

    CCL::AI::RunStatus Tick(CCL::AI::ExecContext &ctx) override
    {
        auto *inst = static_cast<Instance *>(ctx.instance);
        if (!inst) return CCL::AI::RunStatus::Failed;

        inst->timer += ctx.dt;

        // 2秒経過したらワープする
        if (inst->timer >= 2.0f)
        {
            CCL_LOG_SUCCESS(LogCategory::Game, "[DemoAI] Idle終了！原点へワープし、再度Moveへ移行します");
            inst->timer = 0.0f;

            // 肉体(Transform)を強制的に原点に戻す
            auto *tf = ctx.world->GetComponent<TransformComponent>(ctx.entity);
            if (tf)
            {
                tf->position = {0.0f, 0.0f, 0.0f};
            }
        }

        return CCL::AI::RunStatus::Running;
    }
};

struct DemoMoveTask : public CCL::AI::ITask
{
    struct Params
    {
        float targetX = 10.0f;
        float targetZ = 10.0f;
        float speed   = 4.0f;
    };

    static const std::vector<CCL::AI::NodeParamField> &ParamFields()
    {
        static const std::vector<CCL::AI::NodeParamField> f = {
            NODE_PARAM_FLOAT(Params, targetX, "Target X", 10.0f, -50.0f, 50.0f),
            NODE_PARAM_FLOAT(Params, targetZ, "Target Z", 10.0f, -50.0f, 50.0f),
            NODE_PARAM_FLOAT(Params, speed, "Speed", 4.0f, 0.0f, 20.0f),
        };
        return f;
    }

    CCL::AI::RunStatus Tick(CCL::AI::ExecContext &ctx) override
    {
        const auto *p  = static_cast<const Params *>(ctx.params);
        auto       *tf = ctx.world->GetComponent<TransformComponent>(ctx.entity);
        if (!p || !tf) return CCL::AI::RunStatus::Failed;

        DirectX::SimpleMath::Vector3 target(p->targetX, tf->position.y, p->targetZ);
        DirectX::SimpleMath::Vector3 dir = target - tf->position;

        if (dir.Length() < 0.1f)
        {
            CCL_LOG_INFO(LogCategory::Game, "[DemoAI] 目標地点に到着しました！");
            return CCL::AI::RunStatus::Succeeded;
        }
        dir.Normalize();
        tf->position = tf->position + dir * p->speed * ctx.dt;
        return CCL::AI::RunStatus::Running;
    }
};

struct IsFarFromTargetCondition : public CCL::AI::ICondition
{
    struct Params
    {
        float targetX   = 10.0f;
        float targetZ   = 10.0f;
        float threshold = 0.2f;
    };

    static const std::vector<CCL::AI::NodeParamField> &ParamFields()
    {
        static const std::vector<CCL::AI::NodeParamField> f = {
            NODE_PARAM_FLOAT(Params, targetX, "Target X", 10.0f, -50.0f, 50.0f),
            NODE_PARAM_FLOAT(Params, targetZ, "Target Z", 10.0f, -50.0f, 50.0f),
            NODE_PARAM_FLOAT(Params, threshold, "Threshold", 0.2f, 0.0f, 5.0f),
        };
        return f;
    }

    bool Evaluate(CCL::AI::ExecContext &ctx) override
    {
        const auto *p  = static_cast<const Params *>(ctx.params);
        const auto *tf = ctx.world->GetComponent<TransformComponent>(ctx.entity);
        if (!p || !tf) return false;

        float dx = p->targetX - tf->position.x;
        float dz = p->targetZ - tf->position.z;
        return (dx * dx + dz * dz) > (p->threshold * p->threshold);
    }
};

struct DemoIDs
{
    CCL::AI::NodeTypeID idleTask;
    CCL::AI::NodeTypeID moveTask;
    CCL::AI::NodeTypeID isFarCond;
};

inline DemoIDs RegisterNodes()
{
    auto   &reg = CCL::AI::NodeRegistry::Instance();
    DemoIDs ids;
    ids.idleTask  = reg.RegisterTask<DemoIdleTask>("DemoIdle", "Demo");
    ids.moveTask  = reg.RegisterTask<DemoMoveTask>("DemoMove", "Demo");
    ids.isFarCond = reg.RegisterCondition<IsFarFromTargetCondition>("IsFarFromTarget", "Demo");
    return ids;
}

inline CCL::AI::StateTreeAsset BuildDemoAsset(const DemoIDs &ids)
{
    using namespace CCL::AI;
    StateTreeAsset asset;
    asset.name = "DemoEnemy";

    const uint16_t root = asset.AddState("Root");
    const uint16_t move = asset.AddState("Move", root);
    {
        IsFarFromTargetCondition::Params cp{.targetX = 10.0f, .targetZ = 10.0f, .threshold = 0.2f};
        asset.AddEnterCondition(move, ids.isFarCond, asset.AddParams(cp), sizeof(cp));

        DemoMoveTask::Params tp{.targetX = 10.0f, .targetZ = 10.0f, .speed = 4.0f};
        asset.AddTask(move, ids.moveTask, asset.AddParams(tp), sizeof(tp), 0);
    }

    const uint16_t idle = asset.AddState("Idle", root);
    {
        asset.AddTask(idle, ids.idleTask, 0, 0, sizeof(DemoIdleTask::Instance));
    }
    return asset;
}
} // namespace DemoAI