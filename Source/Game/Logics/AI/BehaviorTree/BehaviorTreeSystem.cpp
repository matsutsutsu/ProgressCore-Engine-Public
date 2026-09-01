// 汎用 BehaviorTree 評価エンジンの実装。
//        BossAISystem のエンジン部分を registry 駆動で書き直した。
//        ExecuteAction/EvaluateCondition の巨大switchは廃止。
#include "BehaviorTreeSystem.h"

#include "Game/Logics/AI/BehaviorTree/Data/BTAssetCache.h"
#include "Game/Logics/AI/BehaviorTree/Actions/BTActionRegistry.h"

#include "Game/Logics/AI/BehaviorTree/Actions/BTConditions.h"       // 汎用条件
#include "Game/Logics/Character/Boss/Actions/BTConditions_Boss.h"   // ボス固有条件
#include "Game/Logics/Character/Boss/Actions/BTActions_Boss.h"

#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include "Engine/Platform/Core/Logger.h"
#include "Engine/Core/App/GameFlowState.h" // カットシーン中はAIを止める

#include <algorithm>
#include <cstdio>

#include <imgui.h>
// デバッグ用に使うのでここに置く
#include "Game/Logics/Character/Boss/Components/BossCommandComponent.h"
 

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
}

void BehaviorTreeSystem::Initialize()
{
    // 敵の種類を足したら、その敵の RegisterAll をここに1行足す。
    //   エンジン側のこの1行だけがBTの語彙を集める入口。自己登録マクロにもできるが、
    //   「どの敵の行動が生きているか」が1画面で分かる利点のほうが今の規模では大きい。
    BT::CommonConditions::RegisterAll();
    BT::BossConditions::RegisterAll();
    BT::BossActions::RegisterAll();
    CCL_LOG_INFO(LogCategory::AI, "[BehaviorTreeSystem] BTノード登録完了");
}


void BehaviorTreeSystem::SyncAssets()
{
    auto &cache = BTAssetCache::Instance();

    // 直列で回す。ここでやることは「パスに対応する木を受け取り直す」だけで、
    // 変化が無ければポインタ比較1回で終わる。
    //   以前はこの処理が並列ForEachの内側にあり、ファイル読み込み・JSONパース・
    //   ロガー呼び出しがワーカースレッドから同時に走っていた。
    ForEach([&](BehaviorTreeComponent    &btComp,
                const TransformComponent & /*trans*/,
                const TimeStateComponent & /*time*/)
        {
        // 1. パスに対応する木を受け取り直す
        std::shared_ptr<const BTAsset> desired =
            btComp.assetPath.empty() ? nullptr : cache.GetOrLoad(btComp.assetPath);

        if (desired == btComp.sharedAsset) return; // 大半のフレームはここで終わる

        btComp.sharedAsset     = desired;
        btComp.loadedAssetPath = desired ? btComp.assetPath : std::string();

        // 木が入れ替わったら実行状態は捨てる。
        //   前の木の「途中まで進んだシーケンス」や Wait の残り時間を持ち越すと、
        //   差し替え直後の1回だけ挙動がおかしくなって原因が分からなくなる。
        //   バッファの確保をここに集約したので、評価側（並列）は一切確保しない。
        const size_t nodeCount = desired ? desired->nodes.size() : 0;
        btComp.debugNodeStates.assign(nodeCount, BTDebugState::None);
        btComp.nodeTimers.assign(nodeCount, -1.0f);
        btComp.runningNodes.assign(nodeCount, -1);
        btComp.runningNodeId = 0xFFFF;
    });
}


void BehaviorTreeSystem::Update(float rawDt)
{
    if (!m_world->HasResource<GameFlowState>()) return;

    // カットシーン中(導入カメラ演出など)はAIの思考を止める
    const bool cinematic = m_world->GetResource<GameFlowState>().cinematic;

    // --- ① アセットの解決 ---
    //   木の読み込みは共有物の準備であって、個体ごとの処理ではない。
    //   並列ループへ持ち込まないことで、ファイルI/Oもログもメインスレッドに留まる。
    SyncAssets();

    // --- ② 評価（並列）---
    ForEachWithIDParallel([this, cinematic](EntityID entity,
                                 BehaviorTreeComponent& btComp,
                                 const TransformComponent& trans,
                                 const TimeStateComponent& time)
    {
        float dt = time.localDt;

        if (!btComp.sharedAsset || btComp.sharedAsset->nodes.empty()) return;

        const size_t nodeCount = btComp.sharedAsset->nodes.size();

        // バッファは SyncAssets が木と同じ長さに揃えてある。ここで確保はしない
        // （並列ループ内で std::vector を伸ばすのを避けるため）。
        // 万一ずれていたら、その個体だけこのフレームを見送る。
        if (btComp.nodeTimers.size() != nodeCount) return;

        // --- デバッグ：評価を止める ---
        //   タイマー減算より前に抜ける。止めている間も Wait が進むと、再開した瞬間に
        //   溜まった分が一気に消化されて枝が飛ぶ。木は「凍る」のが正しい。
        //   状態表示だけは消しておかないと、BTウィンドウに緑が残って動いて見える。
        //   カットシーン中も同じ扱い（凍らせる）。演出を見ている間にボスが
        //   攻撃を始めていると、演出が明けた瞬間に被弾している事故が起きる。
        if (btComp.debugPaused || cinematic) {
            std::fill(btComp.debugNodeStates.begin(), btComp.debugNodeStates.end(), BTDebugState::None);
            btComp.runningNodeId = 0xFFFF;
            return;
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
        btComp.runningNodeId = 0xFFFF;

        // --- デバッグ：強制ノードが切り替わったら実行状態を捨てる ---
        //   前の技の「途中まで進んだシーケンス」や Wait の残り時間が新しい枝へ
        //   持ち越されると、1回目だけ挙動がおかしくなって原因が分からなくなる。
        if (btComp.debugForcedNodeId != btComp.debugAppliedForcedNodeId) {
            btComp.debugAppliedForcedNodeId = btComp.debugForcedNodeId;
            std::fill(btComp.runningNodes.begin(), btComp.runningNodes.end(), -1);
            std::fill(btComp.nodeTimers.begin(),   btComp.nodeTimers.end(),   -1.0f);
        }

        // --- 評価実行 ---
        //   強制指定があればその枝から、無ければ通常どおりルート(0)から。
        //   エンジン側の変更はこの1行だけで、木の構造には一切触れない。
        BTNodeID startNode = 0;
        if (btComp.debugForcedNodeId >= 0 && btComp.debugForcedNodeId < static_cast<int>(nodeCount)) {
            startNode = static_cast<BTNodeID>(btComp.debugForcedNodeId);
        }
        EvaluateTreeRecursive(startNode, btComp, trans, *btComp.sharedAsset, entity, dt);
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
            ctx.world       = m_world;
            ctx.entity      = entity;
            ctx.bt          = &btComp;
            ctx.myTransform = &myTrans;
            ctx.nodeId      = nodeId;
            ctx.dt          = dt;
            ctx.param       = node.param;
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
            ctx.world       = m_world;
            ctx.entity      = entity;
            ctx.bt          = &btComp;
            ctx.myTransform = &myTrans;
            ctx.nodeId      = nodeId;
            ctx.dt          = dt;
            ctx.param       = node.param;
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
                int retryCount = static_cast<int>(node.param);
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
                if (result == BTNodeState::Success) btComp.nodeTimers[nodeId] = node.param;
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


namespace {
    // ActionRegistry.h の辞書から表示名を逆引きする。見つからなければ nullptr。
    const char *LookupEntryName(const ActionEntry *table, int count, ActionID id)
    {
        for (int i = 0; i < count; ++i) {
            if (table[i].id == id) return table[i].name;
        }
        return nullptr;
    }

    // この枝で最初に見つかる Action の表示名。
    //
    // Sequence や Selector は名前を持たない（名前はエディタのグラフ側にしか無い）ので、
    // 「その枝が何をする枝なのか」を中身の先頭アクションで示す。
    // ドローン攻撃は必ず「陣形指示 → Wait → Strike → …」の順なので、
    // 先頭アクション名がそのまま技名になる。
    const char *FirstActionNameInSubtree(const BTAsset &asset, BTNodeID nodeId)
    {
        const BTNode &node = asset.nodes[nodeId];
        if (node.type == BTNodeType::Action) {
            return LookupEntryName(g_ActionRegistry, g_ActionRegistryCount, node.actionOrConditionId);
        }
        for (BTNodeID child : node.children) {
            if (const char *name = FirstActionNameInSubtree(asset, child)) return name;
        }
        return nullptr;
    }

    // param を短く整形する。小数第1位まで（10.0 → "10.0"）。
    std::string FormatParam(float v)
    {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.1f", v);
        return buf;
    }

    const char *NodeTypeName(BTNodeType type)
    {
        switch (type) {
        case BTNodeType::Selector:       return "Selector";
        case BTNodeType::Sequence:       return "Sequence";
        case BTNodeType::Condition:      return "Condition";
        case BTNodeType::Action:         return "Action";
        case BTNodeType::Decorator:      return "Decorator";
        case BTNodeType::RandomSelector: return "RandomSelector";
        default:                         return "Unknown";
        }
    }
}


void BehaviorTreeSystem::DrawNodeRowRecursive(const BTAsset         &asset,
                                              BTNodeID               nodeId,
                                              BehaviorTreeComponent &btComp,
                                              EntityID               entity)
{
    const BTNode &node = asset.nodes[nodeId];

    // --- 行のラベル（種類＋中身が分かる名前）---
    std::string label = NodeTypeName(node.type);
    if (node.type == BTNodeType::Action) {
        const char *name = LookupEntryName(g_ActionRegistry, g_ActionRegistryCount, node.actionOrConditionId);
        label += name ? std::string(" : ") + name
                      : std::string(" : id=") + std::to_string(node.actionOrConditionId);
        if (GetActionParamLabel(node.actionOrConditionId))
            label += " (" + FormatParam(node.param) + ")";
    }
    else if (node.type == BTNodeType::Condition) {
        const char *name = LookupEntryName(g_ConditionRegistry, g_ConditionRegistryCount, node.actionOrConditionId);
        label += name ? std::string(" : ") + name
                      : std::string(" : id=") + std::to_string(node.actionOrConditionId);
        // 間合いは param に入っている。出さないと「距離が N m 未満」が並ぶだけで
        // どの行が何mの判定なのか分からない。
        if (GetConditionParamLabel(node.actionOrConditionId))
            label += " (" + FormatParam(node.param) + ")";
    }
    else if (const char *hint = FirstActionNameInSubtree(asset, nodeId)) {
        label += std::string(" [") + hint + "]";
    }

    ImGui::PushID(static_cast<int>(nodeId));

    // --- 実行ボタン（もう一度押すと解除）---
    const bool isForced = (btComp.debugForcedNodeId == static_cast<int>(nodeId));
    if (ImGui::SmallButton(isForced ? "解除" : "実行")) {
        btComp.debugForcedNodeId = isForced ? -1 : static_cast<int>(nodeId);

        // 切り替えの後始末：前の技の「今撃て」信号が残ると、止めたはずのドローンが
        // 飛び続けて何を見ているのか分からなくなる。
        // 評価エンジン側はボス固有の概念を持たない方針なので、これはデバッグUIだけで行う。
        if (auto *cmd = m_world->GetComponent<BossCommandComponent>(entity)) {
            cmd->droneAttackActive = false;
        }
    }
    ImGui::SameLine();

    // --- 今フレームの評価結果で色を変える（走っている枝が一目で分かる）---
    ImVec4 color(0.75f, 0.75f, 0.75f, 1.0f);
    if (nodeId < btComp.debugNodeStates.size()) {
        switch (btComp.debugNodeStates[nodeId]) {
        case BTDebugState::Running: color = ImVec4(1.00f, 0.85f, 0.20f, 1.0f); break;
        case BTDebugState::Success: color = ImVec4(0.40f, 1.00f, 0.50f, 1.0f); break;
        case BTDebugState::Failure: color = ImVec4(0.90f, 0.45f, 0.40f, 1.0f); break;
        default: break;
        }
    }

    if (node.children.empty()) {
        ImGui::TextColored(color, "#%d %s", static_cast<int>(nodeId), label.c_str());
    }
    else {
        ImGui::PushStyleColor(ImGuiCol_Text, color);
        const bool open = ImGui::TreeNodeEx("##node", ImGuiTreeNodeFlags_DefaultOpen,
                                            "#%d %s", static_cast<int>(nodeId), label.c_str());
        ImGui::PopStyleColor();
        if (open) {
            for (BTNodeID child : node.children) {
                DrawNodeRowRecursive(asset, child, btComp, entity);
            }
            ImGui::TreePop();
        }
    }

    ImGui::PopID();
}


void BehaviorTreeSystem::DrawForcedNodeGui(BehaviorTreeComponent &btComp, EntityID entity)
{
    if (!btComp.sharedAsset || btComp.sharedAsset->nodes.empty()) {
        ImGui::TextDisabled("BT が読み込まれていません");
        return;
    }

    // 木を凍らせる。調整中の指示が次のフレームで別の技に塗り潰されるのを止める。
    //   移動中に止めるとボスが歩き続けるので、行動要求も併せて切る。
    bool paused = btComp.debugPaused;
    if (ImGui::Checkbox("AIを止める（調整に集中）", &paused)) {
        btComp.debugPaused = paused;
        if (paused) {
            if (auto *cmd = m_world->GetComponent<BossCommandComponent>(entity)) {
                cmd->currentActionId = 0; // None → BossActionSystem は Idle
            }
        }
    }
    if (btComp.debugPaused) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "停止中（陣形指示は保持される）");
    }

    if (btComp.debugForcedNodeId >= 0) {
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f),
                           "強制実行中: #%d", btComp.debugForcedNodeId);
        ImGui::SameLine();
        if (ImGui::SmallButton("通常のBTへ戻す")) {
            btComp.debugForcedNodeId = -1;
            if (auto *cmd = m_world->GetComponent<BossCommandComponent>(entity)) {
                cmd->droneAttackActive = false;
            }
        }
    }
    else {
        ImGui::TextDisabled("通常評価（ルートから）");
    }

    ImGui::TextDisabled("「実行」を押した枝だけを繰り返し評価します（条件・抽選を無視）。");
    ImGui::TextDisabled("技を丸ごと見るなら Sequence の行を、単発で見るなら Action の行を押す。");
    ImGui::Separator();

    DrawNodeRowRecursive(*btComp.sharedAsset, 0, btComp, entity);
}


void BehaviorTreeSystem::OnGui()
{
    if (!ImGui::CollapsingHeader("Behavior Tree (Brain Debugger)")) return;

    auto view = m_world->View<BehaviorTreeComponent>();
    for (auto e : view)
    {
        auto *btComp = m_world->GetComponent<BehaviorTreeComponent>(e);
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

        // 木を並べて「この技を今すぐ出す」を選べるようにする
        if (ImGui::TreeNodeEx("強制実行（デバッグ）", ImGuiTreeNodeFlags_DefaultOpen)) {
            DrawForcedNodeGui(*btComp, e);
            ImGui::TreePop();
        }

        ImGui::PopID();
    }
}


REGISTER_LOGIC_SYSTEM(BehaviorTreeSystem, Priority::LogicStage::L02_Update);
