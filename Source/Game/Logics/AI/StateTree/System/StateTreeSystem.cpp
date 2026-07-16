/// @file  StateTreeSystem.cpp
/// @brief StateTreeSystem の実装（真の汎用・並列駆動版）。
#include "StateTreeSystem.h"

#include "Game/Logics/AI/StateTree/Core/StateTreeAsset.h"
#include "Game/Logics/AI/StateTree/Core/StateTreeRegistry.h"
#include "Game/Logics/AI/StateTree/Core/NodeRegistry.h"

#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include "Engine/Platform/Core/Logger.h"

#include <cstring>  // memset

using namespace CCL::ECS;
using namespace CCL::AI;

// ============================================================
// Initialize
// ============================================================
void StateTreeSystem::Initialize()
{
    if (!_world->HasResource<StateTreeRegistry>()) {
        CCL_LOG_WARN(LogCategory::Game,
            "[StateTreeSystem] StateTreeRegistry が未登録です。");
    }
}

// ============================================================
// Update
// ============================================================
void StateTreeSystem::Update(float /*dt*/)
{
    if (!_world->HasResource<StateTreeRegistry>()) return;
    auto& registry = _world->GetResource<StateTreeRegistry>();

    // ★ 理想の設計：並列化（Parallel）への移行と極小フィルターの適用
    // 引数型・const・参照は、IfSystem のテンプレート定義と完全一致させる。
    ForEachWithIDParallel(
        [&](EntityID entity,
            StateTreeComponent& st,
            const TimeStateComponent& time)
    {
        if (!st.assetHandle.IsValid()) return;

        const StateTreeAsset* asset = registry.Get(st.assetHandle);
        if (!asset || asset->states.empty()) return;

        // ★ 時間管理の絶対ルール：生 dt ではなく time.localDt を徹底適用
        TickStateTree(*asset, st, entity, time.localDt);
    });
}

// ============================================================
// TickStateTree — 選択 → 差分 Enter/Exit → タスク Tick
// ============================================================
void StateTreeSystem::TickStateTree(const StateTreeAsset& asset,
                                    StateTreeComponent&   st,
                                    EntityID              entity,
                                    float                 dt)
{
    uint16_t newPath[StateTreeComponent::MAX_DEPTH] = {};
    uint8_t  newDepth = 0;

    if (!SelectStates(asset, entity, newPath, newDepth)) {
        for (int d = (int)st.activeDepth - 1; d >= 0; --d)
            ExitState(asset, st, st.activeStates[d], entity);
        st.activeDepth = 0;
        return;
    }

    uint8_t common = 0;
    const uint8_t minDepth = (st.activeDepth < newDepth) ? st.activeDepth : newDepth;
    while (common < minDepth && st.activeStates[common] == newPath[common])
        ++common;

    for (int d = (int)st.activeDepth - 1; d >= (int)common; --d)
        ExitState(asset, st, st.activeStates[d], entity);

    for (uint8_t d = 0; d < newDepth; ++d)
        st.activeStates[d] = newPath[d];
    st.activeDepth = newDepth;

    for (uint8_t d = common; d < newDepth; ++d)
        EnterState(asset, st, st.activeStates[d], entity);

    if (st.activeDepth > 0)
        TickTasks(asset, st, st.GetLeafState(), entity, dt);
}

// ============================================================
// SelectStates — ルートから葉まで選択
// ============================================================
bool StateTreeSystem::SelectStates(const StateTreeAsset& asset,
                                   EntityID              entity,
                                   uint16_t              outPath[StateTreeComponent::MAX_DEPTH],
                                   uint8_t&              outDepth)
{
    outDepth = 0;
    uint16_t candidate = asset.states[0].firstChild;

    while (candidate != ST_INVALID) {
        if (outDepth >= StateTreeComponent::MAX_DEPTH) {
            CCL_LOG_WARN(LogCategory::Game,
                "[StateTreeSystem] 状態の深さが MAX_DEPTH 超過。アセット '%s'。", asset.name.c_str());
            break;
        }

        const StateNode& node = asset.states[candidate];

        if (EvaluateEnterConditions(asset, candidate, entity)) {
            outPath[outDepth++] = candidate;
            if (node.childCount > 0) {
                candidate = node.firstChild;
                continue;
            }
            return true;
        }
        candidate = node.nextSibling;
    }
    return outDepth > 0;
}

// ============================================================
// EvaluateEnterConditions
// ============================================================
bool StateTreeSystem::EvaluateEnterConditions(const StateTreeAsset& asset,
                                              uint16_t              stateIdx,
                                              EntityID              entity)
{
    const StateNode& node = asset.states[stateIdx];
    auto& nreg = NodeRegistry::Instance();

    for (uint16_t ci = node.enterCondBegin; ci < node.enterCondBegin + node.enterCondCount; ++ci)
    {
        if (ci >= asset.conditions.size()) break;
        const NodeRef& ref = asset.conditions[ci];

        ICondition* cond = nreg.GetCondition(ref.typeId);
        if (!cond) continue;

        ExecContext ctx;
        ctx.world    = _world;
        ctx.entity   = entity;
        ctx.params   = ref.paramSize > 0 ? asset.paramBlob.data() + ref.paramOffset : nullptr;
        ctx.instance = nullptr;
        ctx.dt       = 0.0f;

        if (!cond->Evaluate(ctx)) return false;
    }
    return true;
}

// ============================================================
// EnterState / ExitState
// ============================================================
void StateTreeSystem::EnterState(const StateTreeAsset& asset,
                                 StateTreeComponent&   st,
                                 uint16_t              stateIdx,
                                 EntityID              entity)
{
    const StateNode& node = asset.states[stateIdx];
    auto& nreg = NodeRegistry::Instance();

    for (uint16_t ti = node.taskBegin; ti < node.taskBegin + node.taskCount; ++ti) {
        if (ti >= asset.tasks.size()) break;
        const NodeRef& ref = asset.tasks[ti];

        ITask* task = nreg.GetTask(ref.typeId);
        if (!task) continue;

        ExecContext ctx;
        ctx.world    = _world;
        ctx.entity   = entity;
        ctx.params   = ref.paramSize > 0 ? asset.paramBlob.data() + ref.paramOffset : nullptr;
        ctx.instance = st.GetInstancePtr(ref.instanceOffset);
        ctx.dt       = 0.0f;

        if (ref.instanceSize > 0 && ctx.instance)
            std::memset(ctx.instance, 0, ref.instanceSize);

        task->EnterState(ctx);
    }
}

void StateTreeSystem::ExitState(const StateTreeAsset& asset,
                                StateTreeComponent&   st,
                                uint16_t              stateIdx,
                                EntityID              entity)
{
    const StateNode& node = asset.states[stateIdx];
    auto& nreg = NodeRegistry::Instance();

    for (uint16_t ti = node.taskBegin; ti < node.taskBegin + node.taskCount; ++ti) {
        if (ti >= asset.tasks.size()) break;
        const NodeRef& ref = asset.tasks[ti];

        ITask* task = nreg.GetTask(ref.typeId);
        if (!task) continue;

        ExecContext ctx;
        ctx.world    = _world;
        ctx.entity   = entity;
        ctx.params   = ref.paramSize > 0 ? asset.paramBlob.data() + ref.paramOffset : nullptr;
        ctx.instance = st.GetInstancePtr(ref.instanceOffset);
        ctx.dt       = 0.0f;

        task->ExitState(ctx);
    }
}

// ============================================================
// TickTasks
// ============================================================
void StateTreeSystem::TickTasks(const StateTreeAsset& asset,
                                StateTreeComponent&   st,
                                uint16_t              stateIdx,
                                EntityID              entity,
                                float                 dt)
{
    const StateNode& node = asset.states[stateIdx];
    auto& nreg = NodeRegistry::Instance();

    for (uint16_t ti = node.taskBegin; ti < node.taskBegin + node.taskCount; ++ti) {
        if (ti >= asset.tasks.size()) break;
        const NodeRef& ref = asset.tasks[ti];

        ITask* task = nreg.GetTask(ref.typeId);
        if (!task) continue;

        ExecContext ctx;
        ctx.world    = _world;
        ctx.entity   = entity;
        ctx.params   = ref.paramSize > 0 ? asset.paramBlob.data() + ref.paramOffset : nullptr;
        ctx.instance = st.GetInstancePtr(ref.instanceOffset);
        ctx.dt       = dt;

        task->Tick(ctx);
    }
}

// ============================================================
// 自動登録
// ============================================================
// Commander(StateTree) が BT の assetPath を書き換えてから BehaviorTreeSystem が読む
REGISTER_LOGIC_SYSTEM(StateTreeSystem, Priority::LogicStage::L02_PreUpdate)