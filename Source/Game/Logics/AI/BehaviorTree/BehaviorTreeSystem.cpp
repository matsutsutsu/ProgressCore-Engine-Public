/// @file  BehaviorTreeSystem.cpp
/// @brief 汎用 BehaviorTree 評価エンジンの実装。
///        BossAISystem のエンジン部分を registry 駆動で書き直した。
///        ExecuteAction/EvaluateCondition の巨大switchは廃止。
#include "BehaviorTreeSystem.h"

#include "Game/Logics/AI/BehaviorTree/Data/BehaviorTreeLoader.h"
#include "Game/Logics/AI/BehaviorTree/Actions/BTActionRegistry.h"

#include "Game/Logics/AI/BehaviorTree/Actions/BTConditions.h"
#include "Game/Logics/Character/Boss/Data/BTActions_Boss.h"

#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include "Engine/Platform/Core/Logger.h"

#include <algorithm>

#ifdef ENABLE_EDITOR
#include <imgui.h>
#endif
 

using namespace CCL::ECS;
using namespace CCL::AI;
using namespace CCL::AI::BT;

namespace {
    // 特定のサブツリー配下の Action ノードのタイマーだけをリセットする
    void ResetChildActionsRecursive(BTNodeID nodeId, BehaviorTreeComponent& btComp, const BTAsset& asset)
    {
        const auto& node = asset.nodes[nodeId];
        if (node.type == BTNodeType::Action) {
            btComp.nodeTimers[nodeId] = -1.0f;
        }
        for (auto childId : node.children) {
            ResetChildActionsRecursive(childId, btComp, asset);
        }
    }
}


BehaviorTreeSystem::BehaviorTreeSystem() : IfSystem("BehaviorTreeSystem")
{
    hasGui = true;
}

void BehaviorTreeSystem::Initialize()
{
    BT::BossConditions::RegisterAll();
    BT::BossActions::RegisterAll();
    CCL_LOG_INFO(LogCategory::AI, "[BehaviorTreeSystem] BTノード登録完了");
}


void BehaviorTreeSystem::Update(float rawDt)
{
    ForEachWithIDParallel([this](EntityID entity,
                                 BehaviorTreeComponent& btComp,
                                 const TransformComponent& trans,
                                 const TimeStateComponent& time)
    {
        float dt = time.localDt;

        // --- 動的ロード（assetPath が変わったら読み直す）---
        if (btComp.assetPath != btComp.loadedAssetPath) {
            if (!btComp.assetPath.empty()) {
                if (!btComp.sharedAsset) {
                    btComp.sharedAsset = std::make_shared<BTAsset>();
                }
                if (BehaviorTreeLoader::LoadFromJson(btComp.assetPath, *btComp.sharedAsset)) {
                    btComp.loadedAssetPath = btComp.assetPath;
                }
                else {
                    CCL_LOG_WARN(LogCategory::AI,
                        "[BehaviorTreeSystem] BTロード失敗のため再試行可能: %s",
                        btComp.assetPath.c_str());
                }
            }
            else {
                btComp.loadedAssetPath.clear();
            }
        }
        if (!btComp.sharedAsset || btComp.sharedAsset->nodes.empty()) return;

        // --- バッファ確保 ---
        size_t nodeCount = btComp.sharedAsset->nodes.size();
        if (btComp.debugNodeStates.size() != nodeCount) {
            btComp.debugNodeStates.resize(nodeCount, BTDebugState::None);
            btComp.nodeTimers.resize(nodeCount, -1.0f);
            btComp.runningNodes.resize(nodeCount, -1);
        }

        // --- タイマー減算 ---
        for (size_t i = 0; i < btComp.nodeTimers.size(); ++i) {
            if (btComp.nodeTimers[i] > 0.0f) {
                btComp.nodeTimers[i] -= dt;
                if (btComp.nodeTimers[i] <= 0.0f) btComp.nodeTimers[i] = 0.0f;
            }
        }
        std::fill(btComp.debugNodeStates.begin(), btComp.debugNodeStates.end(), BTDebugState::None);

        // --- Running しおりのリセット ---
        btComp.previousRunningNodeId = btComp.runningNodeId;
        btComp.runningNodeId = 0xFFFF;

        // --- 評価実行 ---
        EvaluateTreeRecursive(0, btComp, trans, *btComp.sharedAsset, entity, dt);
    });
}


BTNodeState BehaviorTreeSystem::EvaluateTreeRecursive(
    BTNodeID nodeId,
    BehaviorTreeComponent& btComp,
    const TransformComponent& myTrans,
    const BTAsset& asset,
    EntityID entity,
    float dt)
{
    const BTNode& node = asset.nodes[nodeId];
    BTNodeState result = BTNodeState::Failure;

    switch (node.type) {

    case BTNodeType::Selector: {
        int runningIndex = btComp.runningNodes[nodeId];
        if (runningIndex >= 0 && runningIndex < (int)node.children.size()) {
            result = EvaluateTreeRecursive(node.children[runningIndex], btComp, myTrans, asset, entity, dt);
            if (result == BTNodeState::Running) break;
            btComp.runningNodes[nodeId] = -1;
            if (result == BTNodeState::Success) break;
        }
        result = BTNodeState::Failure;
        for (size_t i = 0; i < node.children.size(); ++i) {
            result = EvaluateTreeRecursive(node.children[i], btComp, myTrans, asset, entity, dt);
            if (result == BTNodeState::Running) { btComp.runningNodes[nodeId] = (int)i; break; }
            if (result == BTNodeState::Success) { btComp.runningNodes[nodeId] = -1;   break; }
        }
        break;
    }

    case BTNodeType::Sequence: {
        int startIndex = 0;
        int runningIndex = btComp.runningNodes[nodeId];
        if (runningIndex >= 0 && runningIndex < (int)node.children.size()) startIndex = runningIndex;

        result = BTNodeState::Success;
        for (size_t i = startIndex; i < node.children.size(); ++i) {
            result = EvaluateTreeRecursive(node.children[i], btComp, myTrans, asset, entity, dt);
            if (result == BTNodeState::Running) { btComp.runningNodes[nodeId] = (int)i; break; }
            if (result == BTNodeState::Failure) {
                btComp.runningNodes[nodeId] = -1;
                ResetChildActionsRecursive(nodeId, btComp, asset);
                break;
            }
        }
        if (result == BTNodeState::Success) {
            btComp.runningNodes[nodeId] = -1;
            ResetChildActionsRecursive(nodeId, btComp, asset);
        }
        break;
    }

    case BTNodeType::Condition: {
        // registry から条件クラスを引いて評価（switch廃止）
        IBTCondition* cond = BTActionRegistry::Instance().GetCondition(node.actionOrConditionId);
        if (cond) {
            BTContext ctx;
            ctx.world       = _world;
            ctx.entity      = entity;
            ctx.bt          = &btComp;
            ctx.myTransform = &myTrans;
            ctx.nodeId      = nodeId;
            ctx.dt          = dt;
            result = cond->Evaluate(ctx) ? BTNodeState::Success : BTNodeState::Failure;
        } else {
            result = BTNodeState::Failure;
        }
        break;
    }

    case BTNodeType::Action: {
        // registry からアクションクラスを引いて実行（switch廃止）
        IBTAction* action = BTActionRegistry::Instance().GetAction(node.actionOrConditionId);
        if (action) {
            BTContext ctx;
            ctx.world       = _world;
            ctx.entity      = entity;
            ctx.bt          = &btComp;
            ctx.myTransform = &myTrans;
            ctx.nodeId      = nodeId;
            ctx.dt          = dt;
            result = action->Execute(ctx);
        } else {
            result = BTNodeState::Failure;
        }
        break;
    }

    case BTNodeType::Decorator: {
        int decType = static_cast<int>(node.decoratorType);
        if (decType == 1 && btComp.nodeTimers[nodeId] > 0.0f) {   // Cooldown中
            result = BTNodeState::Failure;
            break;
        }
        if (!node.children.empty()) {
            BTNodeID childId = node.children[0];
            if (decType == 2) {   // Retry
                int retryCount = static_cast<int>(node.decoratorParam);
                for (int r = 0; r < retryCount; ++r) {
                    result = EvaluateTreeRecursive(childId, btComp, myTrans, asset, entity, dt);
                    if (result != BTNodeState::Failure) break;
                }
            } else {
                result = EvaluateTreeRecursive(childId, btComp, myTrans, asset, entity, dt);
            }

            if (decType == 0) {   // Inverter
                if (result == BTNodeState::Success)      result = BTNodeState::Failure;
                else if (result == BTNodeState::Failure) result = BTNodeState::Success;
            }
            else if (decType == 1) {   // Cooldown（成功時にタイマー開始）
                if (result == BTNodeState::Success) btComp.nodeTimers[nodeId] = node.decoratorParam;
            }
            else if (decType == 3) {   // Async
                if (result == BTNodeState::Running) {
                    const BTNode& childNode = asset.nodes[childId];
                    if (childNode.type == BTNodeType::Action) {
                        result = (btComp.nodeTimers[childId] != -1.0f)
                            ? BTNodeState::Success : BTNodeState::Running;
                    } else {
                        result = BTNodeState::Success;
                    }
                }
            }
        }
        break;
    }

    case BTNodeType::RandomSelector: {
        int runningIndex = btComp.runningNodes[nodeId];
        if (runningIndex >= 0 && runningIndex < (int)node.children.size()) {
            result = EvaluateTreeRecursive(node.children[runningIndex], btComp, myTrans, asset, entity, dt);
            if (result == BTNodeState::Running) break;
            btComp.runningNodes[nodeId] = -1;
            break;
        }
        constexpr int MAX_CHILDREN = 16;
        BTNodeID shuffled[MAX_CHILDREN];
        int count = (std::min)((int)node.children.size(), MAX_CHILDREN);
        for (int i = 0; i < count; ++i) shuffled[i] = node.children[i];
        for (int i = count - 1; i > 0; --i) { int j = rand() % (i + 1); std::swap(shuffled[i], shuffled[j]); }

        result = BTNodeState::Failure;
        for (int i = 0; i < count; ++i) {
            BTNodeID childId = shuffled[i];
            result = EvaluateTreeRecursive(childId, btComp, myTrans, asset, entity, dt);
            if (result == BTNodeState::Running) {
                for (size_t origIdx = 0; origIdx < node.children.size(); ++origIdx) {
                    if (node.children[origIdx] == childId) { btComp.runningNodes[nodeId] = (int)origIdx; break; }
                }
                break;
            }
            if (result == BTNodeState::Success) { btComp.runningNodes[nodeId] = -1; break; }
        }
        break;
    }
    }

    // デバッグ状態の保存
    BTDebugState dbgState = BTDebugState::None;
    if (result == BTNodeState::Success)      dbgState = BTDebugState::Success;
    else if (result == BTNodeState::Failure) dbgState = BTDebugState::Failure;
    else if (result == BTNodeState::Running) dbgState = BTDebugState::Running;
    btComp.debugNodeStates[nodeId] = dbgState;

    if (result == BTNodeState::Running && btComp.runningNodeId == 0xFFFF) {
        btComp.runningNodeId = nodeId;
    }
    return result;
}


void BehaviorTreeSystem::OnGui()
{
    if (!ImGui::CollapsingHeader("Behavior Tree (Brain Debugger)")) return;

    auto view = _world->View<BehaviorTreeComponent>();
    for (auto e : view)
    {
        auto *btComp = _world->GetComponent<BehaviorTreeComponent>(e);
        if (!btComp) continue;

        ImGui::PushID(static_cast<int>(e));
        ImGui::Text("Entity ID: %llu", (unsigned long long)e);
        ImGui::Text("Asset Path: %s", btComp->assetPath.c_str());

        // lastLogActionId から行動名を逆引き（ActionRegistry.hの辞書を利用）
        std::string actionName = "Idle";
        for (int i = 0; i < g_ActionRegistryCount; ++i)
        {
            if (g_ActionRegistry[i].id == btComp->lastLogActionId)
            {
                actionName = g_ActionRegistry[i].name;
                break;
            }
        }
        ImGui::TextColored(ImVec4(0.4f, 1.0f, 1.0f, 1.0f), "Current Action: %s", actionName.c_str());

        ImGui::Text("Distance To Target: %.2fm", btComp->blackboard.distanceToTarget);
        ImGui::Text("HP: %.1f%%", btComp->blackboard.healthPercentage);

        ImGui::PopID();
    }
}


REGISTER_LOGIC_SYSTEM(BehaviorTreeSystem, Priority::LogicStage::L02_Update);