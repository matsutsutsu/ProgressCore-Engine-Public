/// @file  StateTreeWindow.cpp
/// @brief StateTreeWindow の実装（コア ImGui のみ・外部ノードエディタ不要）。
#include "StateTreeWindow.h"

#include "Game/Logics/AI/StateTree/NodeRegistry.h"
#include "Game/Logics/AI/StateTree/StateTreeSerializer.h"
#include "Game/Logics/AI/StateTree/StateTreeRegistry.h"
#include "Game/Logics/AI/StateTree/StateTreeComponent.h"

#include "Engine/Platform/Core/Dialog.h"          // Engine::FileDialog（パスは適宜）
#include "Engine/ECS/Core/CCL_World.h"

#ifdef ENABLE_EDITOR
#include <imgui.h>
#endif

using namespace CCL::AI;

// ============================================================
// ctor
// ============================================================
StateTreeWindow::StateTreeWindow()
    : EditorWindow("State Tree Editor", false)
{
    // 起動時に空ツリーを用意
    NewTree();

#ifdef ENABLE_EDITOR
    // ファイルダイアログのプリセットを登録（StateTree 専用の履歴を持つ）
    Engine::FileDialog::RegisterPreset("StateTree", {
        /*filters*/   { { "StateTree", "*.json" } },
        /*defaultDir*/ "Assets/AI",
        /*defaultExt*/ "json"
    });
#endif
}

// ============================================================
// DrawContents
// ============================================================
void StateTreeWindow::DrawContents(EditorContext& context)
{
#ifdef ENABLE_EDITOR
    DrawToolbar(context);
    ImGui::Separator();

    // 左：ツリー / 右：インスペクタ
    const float leftW = 260.0f;
    ImGui::BeginChild("st_tree", ImVec2(leftW, 0), true);
    DrawTreePanel();
    ImGui::EndChild();

    ImGui::SameLine();

    ImGui::BeginChild("st_inspector", ImVec2(0, 0), true);
    DrawInspectorPanel();
    ImGui::Separator();
    DrawDebugPanel(context);
    ImGui::EndChild();

    // 描画後に遅延構造操作を適用（vector 破壊の回避）
    if (_deferredOp == DeferredOp::AddChild) {
        if (auto* s = Resolve(_deferredPath)) {
            StateDef child; child.name = "NewState";
            s->children.push_back(child);
        }
        _deferredOp = DeferredOp::None;
    } else if (_deferredOp == DeferredOp::DeleteState) {
        if (!_deferredPath.empty()) {
            std::vector<int> parentPath = _deferredPath;
            const int childIdx = parentPath.back();
            parentPath.pop_back();
            if (auto* parent = Resolve(parentPath)) {
                if (childIdx >= 0 && childIdx < (int)parent->children.size())
                    parent->children.erase(parent->children.begin() + childIdx);
            }
            _hasSelection = false;
        }
        _deferredOp = DeferredOp::None;
    }
#else
    (void)context;
#endif
}

#ifdef ENABLE_EDITOR
// ============================================================
// Toolbar
// ============================================================
void StateTreeWindow::DrawToolbar(EditorContext& context)
{
    if (ImGui::Button("New"))  NewTree();
    ImGui::SameLine();
    if (ImGui::Button("Open")) OpenTree();
    ImGui::SameLine();
    if (ImGui::Button("Save")) SaveTree(false);
    ImGui::SameLine();
    if (ImGui::Button("Save As")) SaveTree(true);
    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    if (ImGui::Button("Apply to World")) ApplyToWorld(context);

    ImGui::SameLine();
    ImGui::Text("  Tree: %s", _def.name.c_str());
    if (!_currentPath.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("(%s)", _currentPath.c_str());
    }

    // ツリー名の編集
    char nameBuf[128];
    std::snprintf(nameBuf, sizeof(nameBuf), "%s", _def.name.c_str());
    ImGui::SetNextItemWidth(200);
    if (ImGui::InputText("Name", nameBuf, sizeof(nameBuf)))
        _def.name = nameBuf;
}

// ============================================================
// Tree Panel
// ============================================================
void StateTreeWindow::DrawTreePanel()
{
    DrawStateNode(_def.root, {});
}

void StateTreeWindow::DrawStateNode(StateDef& state, std::vector<int> path)
{
    ImGui::PushID(&state);

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow
                             | ImGuiTreeNodeFlags_DefaultOpen
                             | ImGuiTreeNodeFlags_SpanAvailWidth;
    if (state.children.empty()) flags |= ImGuiTreeNodeFlags_Leaf;
    if (path == _selectedPath && _hasSelection) flags |= ImGuiTreeNodeFlags_Selected;

    const bool open = ImGui::TreeNodeEx(state.name.c_str(), flags);

    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
        _selectedPath = path;
        _hasSelection = true;
    }

    // 右クリックメニュー
    if (ImGui::BeginPopupContextItem()) {
        if (ImGui::MenuItem("Add Child State")) {
            _deferredOp   = DeferredOp::AddChild;
            _deferredPath = path;
        }
        // ルート（path 空）は削除不可
        if (!path.empty() && ImGui::MenuItem("Delete State")) {
            _deferredOp   = DeferredOp::DeleteState;
            _deferredPath = path;
        }
        ImGui::EndPopup();
    }

    if (open) {
        for (int i = 0; i < (int)state.children.size(); ++i) {
            std::vector<int> childPath = path;
            childPath.push_back(i);
            DrawStateNode(state.children[i], childPath);
        }
        ImGui::TreePop();
    }

    ImGui::PopID();
}

// ============================================================
// Inspector Panel
// ============================================================
void StateTreeWindow::DrawInspectorPanel()
{
    if (!_hasSelection) {
        ImGui::TextDisabled("状態を選択してください");
        return;
    }
    StateDef* s = Resolve(_selectedPath);
    if (!s) { _hasSelection = false; return; }

    // 状態名
    char nameBuf[128];
    std::snprintf(nameBuf, sizeof(nameBuf), "%s", s->name.c_str());
    if (ImGui::InputText("State Name", nameBuf, sizeof(nameBuf)))
        s->name = nameBuf;

    ImGui::Spacing();
    DrawNodeList("Enter Conditions", s->enterConditions, /*isCondition*/true);
    ImGui::Spacing();
    DrawNodeList("Tasks",            s->tasks,           /*isCondition*/false);
}

void StateTreeWindow::DrawNodeList(const char* label,
                                   std::vector<NodeDef>& nodes,
                                   bool isCondition)
{
    ImGui::SeparatorText(label);

    int removeIdx = -1;
    for (int i = 0; i < (int)nodes.size(); ++i) {
        ImGui::PushID(i);
        ImGui::BulletText("%s", nodes[i].type.c_str());
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 20);
        if (ImGui::SmallButton("x")) removeIdx = i;

        ImGui::Indent();
        DrawParams(nodes[i], isCondition);
        ImGui::Unindent();
        ImGui::PopID();
    }
    if (removeIdx >= 0) nodes.erase(nodes.begin() + removeIdx);

    // 追加ボタン → パレット popup
    const char* popupId = isCondition ? "add_cond_popup" : "add_task_popup";
    if (ImGui::Button(isCondition ? "+ Add Condition" : "+ Add Task"))
        ImGui::OpenPopup(popupId);

    if (ImGui::BeginPopup(popupId)) {
        auto descs = isCondition
            ? NodeRegistry::Instance().GetAllConditionDescs()
            : NodeRegistry::Instance().GetAllTaskDescs();
        for (const auto& d : descs) {
            std::string item = d.category + " / " + d.name;
            if (ImGui::Selectable(item.c_str())) {
                NodeDef n; n.type = d.name;
                nodes.push_back(n);
            }
        }
        ImGui::EndPopup();
    }
}

void StateTreeWindow::DrawParams(NodeDef& node, bool isCondition)
{
    const NodeDesc* desc = isCondition
        ? NodeRegistry::Instance().FindConditionDescByName(node.type)
        : NodeRegistry::Instance().FindTaskDescByName(node.type);
    if (!desc || desc->paramFields.empty()) return;

    for (const auto& f : desc->paramFields) {
        ImGui::PushID(f.name.c_str());
        switch (f.type) {
        case ParamType::Float: {
            float v = node.params.value(f.name, (float)f.defVal);
            if (ImGui::DragFloat(f.name.c_str(), &v, f.step,
                                 (float)f.minVal, (float)f.maxVal))
                node.params[f.name] = v;
        } break;
        case ParamType::Int: {
            int v = node.params.value(f.name, (int)f.defVal);
            if (ImGui::InputInt(f.name.c_str(), &v))
                node.params[f.name] = v;
        } break;
        case ParamType::Bool: {
            bool v = node.params.value(f.name, (bool)(f.defVal != 0.0));
            if (ImGui::Checkbox(f.name.c_str(), &v))
                node.params[f.name] = v;
        } break;
        }
        ImGui::PopID();
    }
}

// ============================================================
// Debug Panel
// ============================================================
void StateTreeWindow::DrawDebugPanel(EditorContext& context)
{
    ImGui::SeparatorText("Runtime Debug");

    if (!context.world) { ImGui::TextDisabled("No world"); return; }
    if (context.selectedEntity == CCL::ECS::InvalidEntityID) {
        ImGui::TextDisabled("Entity 未選択");
        return;
    }

    auto* st = context.world->GetComponent<StateTreeComponent>(context.selectedEntity);
    if (!st) {
        ImGui::TextDisabled("選択 Entity に StateTreeComponent なし");
        return;
    }

    // 実行中アセット名
    const StateTreeAsset* asset = nullptr;
    if (context.world->HasResource<StateTreeRegistry>()) {
        asset = context.world->GetResource<StateTreeRegistry>().Get(st->assetHandle);
    }
    if (asset) ImGui::Text("Asset: %s", asset->name.c_str());
    else       ImGui::Text("Asset: (handle %u)", st->assetHandle.value);

    // アクティブパス
    if (st->activeDepth == 0) {
        ImGui::TextDisabled("(no active state)");
    } else {
        ImGui::Text("Active path:");
        for (int d = 0; d < st->activeDepth; ++d) {
            const uint16_t idx = st->activeStates[d];
            const char* nm = asset ? asset->GetStateName(idx).c_str() : "?";
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), " > %s", nm);
        }
    }
}

// ============================================================
// 操作
// ============================================================
void StateTreeWindow::NewTree()
{
    _def = StateTreeDef{};
    _def.name = "NewStateTree";
    _def.root.name = "Root";
    _currentPath.clear();
    _selectedPath.clear();
    _hasSelection = false;
}

void StateTreeWindow::OpenTree()
{
    auto picked = Engine::FileDialog::OpenPreset("StateTree", "Open State Tree");
    if (!picked) return;

    const std::string rel = Engine::FileDialog::ConvertToRelativePath(*picked);
    StateTreeDef loaded;
    if (StateTreeSerializer::LoadDef(rel, loaded)) {
        _def = std::move(loaded);
        _currentPath = rel;
        _selectedPath.clear();
        _hasSelection = false;
    }
}

void StateTreeWindow::SaveTree(bool saveAs)
{
    std::string target = _currentPath;
    if (saveAs || target.empty()) {
        auto picked = Engine::FileDialog::SavePreset(
            "StateTree", "Save State Tree", _def.name + ".json");
        if (!picked) return;
        target = Engine::FileDialog::ConvertToRelativePath(*picked);
    }
    if (StateTreeSerializer::SaveDef(target, _def))
        _currentPath = target;
}

void StateTreeWindow::ApplyToWorld(EditorContext& context)
{
    if (!context.world) return;
    if (!context.world->HasResource<StateTreeRegistry>())
        context.world->AddResource<StateTreeRegistry>();

    auto& reg = context.world->GetResource<StateTreeRegistry>();
    StateTreeAsset compiled = StateTreeSerializer::Compile(_def);
    reg.Register(std::move(compiled));   // ※同名は新ハンドルとして追加（下記メモ参照）
}

// ============================================================
// path 解決
// ============================================================
StateDef* StateTreeWindow::Resolve(const std::vector<int>& path)
{
    StateDef* cur = &_def.root;
    for (int idx : path) {
        if (idx < 0 || idx >= (int)cur->children.size()) return nullptr;
        cur = &cur->children[idx];
    }
    return cur;
}
#endif // ENABLE_EDITOR
