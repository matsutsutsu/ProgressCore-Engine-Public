#include "Editor/Windows/HierarchyWindow.h"
#include "Game/Core/AllComponents.h"

HierarchyWindow::HierarchyWindow() : EditorWindow("Hierarchy") {}

void HierarchyWindow::DrawContents(EditorContext &context)
{
    auto *world        = context.world;
    auto &chunkManager = world->GetChunkManager();
    auto &chunks       = chunkManager.GetChunks();

    // エンティティ作成ボタン
    if (ImGui::Button("Spawn Entity")) {
        // ★修正: 生成時に必ずNameComponentを付与し、推論ロジックを不要にする
        auto arch   = ArchetypeHelper::Generate<TransformComponent, NameComponent>();
        auto entity = world->RequestSpawnEntity(arch);
        // デフォルト名として空白をセット（インスペクタでID表示にフォールバックされる）
        world->GetComponent<NameComponent>(entity)->name[0] = '\0';
    }
    ImGui::Separator();

    std::vector<CCL::ECS::EntityID>                                         rootEntities;
    std::unordered_map<CCL::ECS::EntityID, std::vector<CCL::ECS::EntityID>> childrenMap;
    std::vector<CCL::ECS::EntityID>                                         allEntities;

    for (auto &chunkPtr : chunks) {
        if (!chunkPtr) continue;
        CCL::ECS::Core::Chunk *chunk = chunkPtr.get();
        const size_t           count = chunk->GetEntityCount();

        NameComponent *names = nullptr;
        if (chunk->HasComponent<NameComponent>()) {
            names = chunk->GetComponentArray<NameComponent>();
        }

        TransformComponent *transforms = nullptr;
        if (chunk->HasComponent<TransformComponent>()) {
            transforms = chunk->GetComponentArray<TransformComponent>();
        }

        for (size_t i = 0; i < count; ++i) {
            CCL::ECS::EntityID entity = chunk->GetEntityByIndex(i);
            if (entity == CCL::ECS::InvalidEntityID) continue;

            std::string entityName;
            if (names && strlen(names[i].name) > 0) {
                entityName = names[i].name;
            }
            else {
                entityName = "Entity " + std::to_string(entity);
            }
            context.entityNames[entity] = entityName;
            allEntities.push_back(entity);

            bool hasParent = false;
            if (transforms) {
                if (transforms[i].parentID != 0) {
                    childrenMap[transforms[i].parentID].push_back(entity);
                    hasParent = true;
                }
            }
            if (!hasParent) {
                rootEntities.push_back(entity);
            }
        }
    }

    for (auto entityID : rootEntities) {
        DrawEntityNode(entityID, context, childrenMap);
    }

    if (ImGui::IsMouseDown(0) && ImGui::IsWindowHovered()) {
        // ★修正: 選択解除
        context.selectedEntity = CCL::ECS::InvalidEntityID;
    }
}

void HierarchyWindow::DrawEntityNode(CCL::ECS::EntityID                            entityID,
    EditorContext                                                                 &context,
    const std::unordered_map<CCL::ECS::EntityID, std::vector<CCL::ECS::EntityID>> &childrenMap)
{
    std::string name   = "Entity " + std::to_string(entityID);
    auto        itName = context.entityNames.find(entityID);
    if (itName != context.entityNames.end() && !itName->second.empty()) {
        name = itName->second;
    }

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
    bool               hasChildren = childrenMap.count(entityID) > 0;
    if (!hasChildren) {
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    }

    // ★修正: ポインタではなく実体どうしを比較
    if (context.selectedEntity == entityID) {
        flags |= ImGuiTreeNodeFlags_Selected;
    }

    bool opened = ImGui::TreeNodeEx((void *)(uintptr_t)entityID, flags, name.c_str());

    // ★修正: ポインタに代入するのではなく、実体に代入する
    if (ImGui::IsItemClicked()) {
        context.selectedEntity = entityID;
    }

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

    if (hasChildren && opened) {
        const auto &children = childrenMap.at(entityID);
        for (auto childID : children) {
            DrawEntityNode(childID, context, childrenMap);
        }
        ImGui::TreePop();
    }

    if (ImGui::BeginPopupContextItem()) {
        if (ImGui::MenuItem("Delete Entity")) {
            context.world->Destroy(entityID);
            // ★修正
            if (context.selectedEntity == entityID)
                context.selectedEntity = CCL::ECS::InvalidEntityID;
        }
        ImGui::EndPopup();
    }
}