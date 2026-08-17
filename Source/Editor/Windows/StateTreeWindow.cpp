/// @file  StateTreeWindow.cpp
/// @brief StateTreeWindow の実装（コア ImGui のみ・外部ノードエディタ不要）。
#include "StateTreeWindow.h"

#include "Game/Logics/AI/StateTree/Core/NodeRegistry.h"
#include "Game/Logics/AI/StateTree/Serialization/StateTreeSerializer.h"
#include "Game/Logics/AI/StateTree/Core/StateTreeRegistry.h"
#include "Game/Logics/AI/StateTree/Core/StateTreeComponent.h"

#include "Engine/Platform/Core/Dialog.h"          // Engine::FileDialog（保存/読込ダイアログ）
#include "Engine/ECS/Core/CCL_World.h"

#ifdef ENABLE_EDITOR
#include <imgui.h>
#endif

#include <utility>   // std::swap（兄弟の並び替えで使用）

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
    FileDialog::RegisterPreset("StateTree", {
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
    if (m_deferredOp == DeferredOp::AddChild) {
        if (auto* s = Resolve(m_deferredPath)) {
            StateDef child; child.name = "NewState";
            s->children.push_back(child);
        }
        m_deferredOp = DeferredOp::None;
    } else if (m_deferredOp == DeferredOp::DeleteState) {
        if (!m_deferredPath.empty()) {
            std::vector<int> parentPath = m_deferredPath;
            const int childIdx = parentPath.back();
            parentPath.pop_back();
            if (auto* parent = Resolve(parentPath)) {
                if (childIdx >= 0 && childIdx < (int)parent->children.size())
                    parent->children.erase(parent->children.begin() + childIdx);
            }
            m_hasSelection = false;
        }
        m_deferredOp = DeferredOp::None;
    } else if (m_deferredOp == DeferredOp::MoveUp || m_deferredOp == DeferredOp::MoveDown) {
        // 兄弟内の並び順 = 評価優先度（SelectStates は先頭から早い者勝ちで評価するため）。
        if (!m_deferredPath.empty()) {
            std::vector<int> parentPath = m_deferredPath;
            const int childIdx = parentPath.back();
            parentPath.pop_back();
            if (auto* parent = Resolve(parentPath)) {
                const int otherIdx = (m_deferredOp == DeferredOp::MoveUp) ? childIdx - 1 : childIdx + 1;
                if (otherIdx >= 0 && otherIdx < (int)parent->children.size()) {
                    std::swap(parent->children[childIdx], parent->children[otherIdx]);

                    // 選択中の状態が動いた側なら選択パスも追従させる
                    std::vector<int> selfPath  = parentPath; selfPath.push_back(childIdx);
                    std::vector<int> otherPath = parentPath; otherPath.push_back(otherIdx);
                    if (m_hasSelection && m_selectedPath == selfPath)       m_selectedPath = otherPath;
                    else if (m_hasSelection && m_selectedPath == otherPath) m_selectedPath = selfPath;
                }
            }
        }
        m_deferredOp = DeferredOp::None;
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
    ImGui::Text("  Tree: %s", m_def.name.c_str());
    if (!m_currentPath.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("(%s)", m_currentPath.c_str());
    }

    // ツリー名の編集
    char nameBuf[128];
    std::snprintf(nameBuf, sizeof(nameBuf), "%s", m_def.name.c_str());
    ImGui::SetNextItemWidth(200);
    if (ImGui::InputText("Name", nameBuf, sizeof(nameBuf)))
        m_def.name = nameBuf;
}

// ============================================================
// Tree Panel
// ============================================================
void StateTreeWindow::DrawTreePanel()
{
    DrawStateNode(m_def.root, {});
}

void StateTreeWindow::DrawStateNode(StateDef& state, std::vector<int> path)
{
    ImGui::PushID(&state);

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow
                             | ImGuiTreeNodeFlags_DefaultOpen
                             | ImGuiTreeNodeFlags_SpanAvailWidth;
    if (state.children.empty()) flags |= ImGuiTreeNodeFlags_Leaf;
    if (path == m_selectedPath && m_hasSelection) flags |= ImGuiTreeNodeFlags_Selected;

    const bool open = ImGui::TreeNodeEx(state.name.c_str(), flags);

    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
        m_selectedPath = path;
        m_hasSelection = true;
    }

    // 右クリックメニュー
    if (ImGui::BeginPopupContextItem()) {
        if (ImGui::MenuItem("Add Child State")) {
            m_deferredOp   = DeferredOp::AddChild;
            m_deferredPath = path;
        }
        // ルート（path 空）は削除・並び替え不可（兄弟が存在しないため）
        if (!path.empty()) {
            if (ImGui::MenuItem("Delete State")) {
                m_deferredOp   = DeferredOp::DeleteState;
                m_deferredPath = path;
            }
            ImGui::Separator();
            // 優先度 = 兄弟内の並び順（上にあるほど先に評価される＝優先度が高い）
            if (ImGui::MenuItem("Move Up")) {
                m_deferredOp   = DeferredOp::MoveUp;
                m_deferredPath = path;
            }
            if (ImGui::MenuItem("Move Down")) {
                m_deferredOp   = DeferredOp::MoveDown;
                m_deferredPath = path;
            }
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
    if (!m_hasSelection) {
        ImGui::TextDisabled("状態を選択してください");
        return;
    }
    StateDef* s = Resolve(m_selectedPath);
    if (!s) { m_hasSelection = false; return; }

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
        case ParamType::String: {
            std::string cur = node.params.value(f.name, f.defStr);
            char        buf[256];
            std::snprintf(buf, sizeof(buf), "%s", cur.c_str());
            if (ImGui::InputText(f.name.c_str(), buf, sizeof(buf)))
                node.params[f.name] = std::string(buf);

            // パス型パラメータ（presetContext あり）は、隣に「参照」ボタンを出して
            // ファイルダイアログから相対パスで選べるようにする（直接入力も維持）。
            if (!f.presetContext.empty()) {
                ImGui::SameLine();
                const std::string btnLabel = std::string("参照##") + f.name;
                if (ImGui::Button(btnLabel.c_str())) {
                    if (auto picked = FileDialog::OpenPreset(f.presetContext))
                        node.params[f.name] = FileDialog::ConvertToRelativePath(*picked);
                }
            }
        }
        break;
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
    m_def = StateTreeDef{};
    m_def.name = "NewStateTree";
    m_def.root.name = "Root";
    m_currentPath.clear();
    m_selectedPath.clear();
    m_hasSelection = false;
}

void StateTreeWindow::OpenTree()
{
    auto picked = FileDialog::OpenPreset("StateTree", "Open State Tree");
    if (!picked) return;

    const std::string rel = FileDialog::ConvertToRelativePath(*picked);
    StateTreeDef loaded;
    if (StateTreeSerializer::LoadDef(rel, loaded)) {
        m_def = std::move(loaded);
        m_currentPath = rel;
        m_selectedPath.clear();
        m_hasSelection = false;
    }
}

void StateTreeWindow::SaveTree(bool saveAs)
{
    std::string target = m_currentPath;
    if (saveAs || target.empty()) {
        auto picked = FileDialog::SavePreset(
            "StateTree", "Save State Tree", m_def.name + ".json");
        if (!picked) return;
        target = FileDialog::ConvertToRelativePath(*picked);
    }
    if (StateTreeSerializer::SaveDef(target, m_def))
        m_currentPath = target;
}

void StateTreeWindow::ApplyToWorld(EditorContext& context)
{
    if (!context.world) return;
    if (!context.world->HasResource<StateTreeRegistry>())
        context.world->AddResource<StateTreeRegistry>();

    auto& reg = context.world->GetResource<StateTreeRegistry>();
    StateTreeAsset compiled = StateTreeSerializer::Compile(m_def);
    reg.Register(std::move(compiled));   // ※同名は新ハンドルとして追加（下記メモ参照）
}

// ============================================================
// path 解決
// ============================================================
StateDef* StateTreeWindow::Resolve(const std::vector<int>& path)
{
    StateDef* cur = &m_def.root;
    for (int idx : path) {
        if (idx < 0 || idx >= (int)cur->children.size()) return nullptr;
        cur = &cur->children[idx];
    }
    return cur;
}
#endif // ENABLE_EDITOR
