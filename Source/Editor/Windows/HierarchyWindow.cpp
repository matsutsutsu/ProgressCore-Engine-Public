#include "Editor/Windows/HierarchyWindow.h"
#include "Game/Core/AllComponents.h"
#include <algorithm>
#include <string>

HierarchyWindow::HierarchyWindow() : EditorWindow("Hierarchy") {}

void HierarchyWindow::DrawContents(EditorContext &context)
{
    auto *world        = context.world;
    auto &chunkManager = world->GetChunkManager();
    auto &chunks       = chunkManager.GetChunks();

    // ---------------------------------------------------------
    // ツールバー (生成 ＆ 検索)
    // ---------------------------------------------------------
    if (ImGui::Button("Spawn Entity")) {
        auto arch   = ArchetypeHelper::Generate<TransformComponent, NameComponent>();
        auto entity = world->RequestSpawnEntity(arch);
        world->GetComponent<NameComponent>(entity)->name[0] = '\0';
    }

    ImGui::SameLine();
    // 検索バーの実装 (1万体を超えるプロジェクトでは必須)
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##Search", "Search Entity...", _searchBuffer, sizeof(_searchBuffer));

    bool        isSearching = strlen(_searchBuffer) > 0;
    std::string searchStr   = _searchBuffer;
    // 小文字に変換して大文字小文字を区別しない検索にする
    std::transform(searchStr.begin(), searchStr.end(), searchStr.begin(), ::tolower);

    ImGui::Separator();

    // ---------------------------------------------------------
    // 1. データ収集フェーズ (ここはC++のネイティブ処理なので1万回でも高速)
    // ---------------------------------------------------------
    std::vector<CCL::ECS::EntityID>                                         rootEntities;
    std::unordered_map<CCL::ECS::EntityID, std::vector<CCL::ECS::EntityID>> childrenMap;
    std::vector<CCL::ECS::EntityID>                                         allEntities;

    for (auto &chunkPtr : chunks) {
        if (!chunkPtr) continue;
        CCL::ECS::Core::Chunk *chunk = chunkPtr.get();
        const size_t           count = chunk->GetEntityCount();

        NameComponent      *names      = chunk->HasComponent<NameComponent>()
                                             ? chunk->GetComponentArray<NameComponent>()
                                             : nullptr;
        TransformComponent *transforms = chunk->HasComponent<TransformComponent>()
                                             ? chunk->GetComponentArray<TransformComponent>()
                                             : nullptr;

        for (size_t i = 0; i < count; ++i) {
            CCL::ECS::EntityID entity = chunk->GetEntityByIndex(i);
            if (entity == CCL::ECS::InvalidEntityID) continue;

            std::string entityName      = (names && strlen(names[i].name) > 0)
                                              ? names[i].name
                                              : "Entity " + std::to_string(entity);
            context.entityNames[entity] = entityName;
            allEntities.push_back(entity);

            bool hasParent = false;
            if (transforms && transforms[i].parentID != 0) {
                childrenMap[transforms[i].parentID].push_back(entity);
                hasParent = true;
            }
            if (!hasParent) {
                rootEntities.push_back(entity);
            }
        }
    }

    // ---------------------------------------------------------
    // 2. 描画フェーズ (Clipperを使った超高速カリング描画)
    // ---------------------------------------------------------

    // Childウィンドウを作らないとClipperが全体の高さを計算できないため必須
    ImGui::BeginChild("HierarchyTree", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);

    if (isSearching) {
        // 検索中: 階層を無視して一致するものだけをフラットにリスト化する
        std::vector<CCL::ECS::EntityID> filteredEntities;
        for (auto e : allEntities) {
            std::string nameLower = context.entityNames[e];
            std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), ::tolower);

            if (nameLower.find(searchStr) != std::string::npos) {
                filteredEntities.push_back(e);
            }
        }

        ImGuiListClipper clipper;
        clipper.Begin((int)filteredEntities.size());
        while (clipper.Step()) {
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                // isFlatSearch = true で描画 (子要素の展開を行わない)
                DrawEntityNode(filteredEntities[i], context, childrenMap, true);
            }
        }
        clipper.End();
    }
    else {
        // 通常時: ルートエンティティに対してClipperを適用
        // ※「すべてを展開」した状態での完全なクリッピングはツリー構造の事前平坦化が必要ですが、
        // 一般的に1万体のオブジェクトの9割はルート（パーティクルや弾など）であるため、
        // ルート階層にClipperをかけるだけで劇的なパフォーマンス改善が見込めます。
        ImGuiListClipper clipper;
        clipper.Begin((int)rootEntities.size());
        while (clipper.Step()) {
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                DrawEntityNode(rootEntities[i], context, childrenMap, false);
            }
        }
        clipper.End();
    }

    ImGui::EndChild(); // HierarchyTree

    // 何もない背景部分をクリックした時「だけ」選択解除する
    if (ImGui::IsMouseClicked(0) && ImGui::IsWindowHovered() && !ImGui::IsAnyItemHovered()) {
        context.selectedEntity = CCL::ECS::InvalidEntityID;
    }
}

void HierarchyWindow::DrawEntityNode(CCL::ECS::EntityID                            entityID,
    EditorContext                                                                 &context,
    const std::unordered_map<CCL::ECS::EntityID, std::vector<CCL::ECS::EntityID>> &childrenMap,
    bool                                                                           isFlatSearch)
{
    std::string name   = "Entity " + std::to_string(entityID);
    auto        itName = context.entityNames.find(entityID);
    if (itName != context.entityNames.end() && !itName->second.empty()) {
        name = itName->second;
    }

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;

    // 検索中、または子がいない場合は「葉（Leaf）」として表示
    bool hasChildren = !isFlatSearch && (childrenMap.count(entityID) > 0);
    if (!hasChildren) {
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    }

    if (context.selectedEntity == entityID) {
        flags |= ImGuiTreeNodeFlags_Selected;
    }

    bool opened = ImGui::TreeNodeEx((void *)(uintptr_t)entityID, flags, name.c_str());

    if (ImGui::IsItemClicked()) {
        context.selectedEntity = entityID;
    }

    // ドラッグ＆ドロップ操作 (検索中は無効化するのが安全)
    if (!isFlatSearch) {
        if (ImGui::BeginDragDropSource()) {
            ImGui::SetDragDropPayload("HIERARCHY_ENTITY", &entityID, sizeof(CCL::ECS::EntityID));
            ImGui::Text("%s", name.c_str());
            ImGui::EndDragDropSource();
        }

        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("HIERARCHY_ENTITY")) {
                CCL::ECS::EntityID childID = *(const CCL::ECS::EntityID *)payload->Data;
                if (childID != entityID) {
                    auto *world      = context.world;
                    auto *childTrans = world->GetComponent<TransformComponent>(childID);
                    if (childTrans) {
                        childTrans->parentID = entityID;
                    }
                }
            }
            ImGui::EndDragDropTarget();
        }
    }

    // 子要素の再帰描画
    if (hasChildren && opened) {
        const auto &children = childrenMap.at(entityID);
        for (auto childID : children) {
            // 子要素の描画（ここも将来的に深さが1万になる場合はフラット化が必要ですが、通常は問題ありません）
            DrawEntityNode(childID, context, childrenMap, false);
        }
        ImGui::TreePop();
    }

    if (ImGui::BeginPopupContextItem()) {
        if (ImGui::MenuItem("Delete Entity")) {
            context.world->Destroy(entityID);
            if (context.selectedEntity == entityID)
                context.selectedEntity = CCL::ECS::InvalidEntityID;
        }
        ImGui::EndPopup();
    }
}