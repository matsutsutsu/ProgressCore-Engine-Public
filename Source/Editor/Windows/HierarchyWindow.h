          #pragma once
#include "Editor/Core/EditorWindow.h"
#include "Game/Core/AllComponents.h"
#include <unordered_map>
#include <vector>

class HierarchyWindow : public EditorWindow {
  public:
    HierarchyWindow() : EditorWindow("Hierarchy") {}

  protected:
    void DrawContents(EditorContext &context) override
    {
        auto *world        = context.world;
        auto &chunkManager = world->GetChunkManager();
        auto &chunks       = chunkManager.GetChunks();

        // エンティティ作成ボタン
        if (ImGui::Button("Spawn Entity")) {
            auto arch = ArchetypeHelper::Generate<TransformComponent>();
            world->RequestSpawnEntity(arch);
        }
        ImGui::Separator();

        // ---------------------------------------------------------
        // 1. 親子関係の構築 (Preprocessing)
        // ---------------------------------------------------------
        std::vector<CCL::ECS::EntityID>                                         rootEntities;
        std::unordered_map<CCL::ECS::EntityID, std::vector<CCL::ECS::EntityID>> childrenMap;
        std::vector<CCL::ECS::EntityID> allEntities; // 全リスト（検索用など）

        // 全チャンク・全エンティティを走査
        for (auto &chunkPtr : chunks) {
            if (!chunkPtr) continue;
            Core::Chunk *chunk = chunkPtr.get();
            const size_t count = chunk->GetEntityCount();

            // ★追加: ChunkからNameComponent配列を取得
            NameComponent *names = nullptr;
            if (chunk->HasComponent<NameComponent>()) {
                names = chunk->GetComponentArray<NameComponent>();
            }

            // Transformコンポーネントの配列を取得（持っていれば）
            // ※Chunk内でコンポーネントは配列管理されている前提
            TransformComponent *transforms = nullptr;
            // ChunkがTransformを含んでいるか確認
            if (chunk->HasComponent<TransformComponent>()) {
                transforms = chunk->GetComponentArray<TransformComponent>();
            }

            for (size_t i = 0; i < count; ++i) {
                CCL::ECS::EntityID entity = chunk->GetEntityByIndex(i);
                if (entity == CCL::ECS::InvalidEntityID) continue;

                // ---------------------------------------------------------
                // ★名前解決: NameComponent があればそれを使う
                // ---------------------------------------------------------
                std::string entityName;
                if (names) {
                    // char配列をそのままstringにして表示
                    entityName = names[i].name;
                }
                else {
                    // なければアーキタイプ推論やID表示
                    entityName = GetInferredName(chunk->GetArchetype());
                    if (entityName.empty()) {
                        entityName = "Entity " + std::to_string(entity);
                    }
                }

                // コンテキストに登録（InspectorWindowなど他でも使えるように）
                context.entityNames[entity] = entityName;


                allEntities.push_back(entity);

                // 親がいるかチェック
                bool hasParent = false;
                if (transforms) {
                    // 同じインデックスiにあるTransformを参照
                    if (transforms[i].parentID != 0) {
                        childrenMap[transforms[i].parentID].push_back(entity);
                        hasParent = true;
                    }
                }

                // 親がいなければルートとして登録
                if (!hasParent) {
                    rootEntities.push_back(entity);
                }
            }
        }

        // ---------------------------------------------------------
        // 2. ツリー描画 (Recursive Draw)
        // ---------------------------------------------------------
        for (auto entityID : rootEntities) {
            DrawEntityNode(entityID, context, childrenMap);
        }

        // 何もないところをクリックしたら選択解除
        if (ImGui::IsMouseDown(0) && ImGui::IsWindowHovered()) {
            // *context.selectedEntity = 0; // 必要なら有効化
        }
    }

  private:
    // 再帰的にノードを描画する関数
    void DrawEntityNode(CCL::ECS::EntityID                                             entityID,
        EditorContext                                                                 &context,
        const std::unordered_map<CCL::ECS::EntityID, std::vector<CCL::ECS::EntityID>> &childrenMap)
    {
        // 名前解決（なければID表示）
        std::string name   = "Entity " + std::to_string(entityID);
        auto        itName = context.entityNames.find(entityID);
        if (itName != context.entityNames.end() && !itName->second.empty()) {
            name = itName->second; // + " (" + std::to_string(entityID) + ")";
        }

        // ツリーノードのフラグ設定
        ImGuiTreeNodeFlags flags =
            ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;

        // 子がいなければリーフ（葉）として扱う
        bool hasChildren = childrenMap.count(entityID) > 0;
        if (!hasChildren) {
            flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
        }

        // 選択状態の反映
        if (context.selectedEntity && *context.selectedEntity == entityID) {
            flags |= ImGuiTreeNodeFlags_Selected;
        }

        // --- ノード描画 ---
        // IDをポインタ代わりにして一意性を保つ
        bool opened = ImGui::TreeNodeEx((void *)(uintptr_t)entityID, flags, name.c_str());

        // クリックで選択
        if (ImGui::IsItemClicked()) {
            *context.selectedEntity = entityID;
        }

        // ---------------------------------------------------------
        // ドラッグ＆ドロップ (Drag & Drop)
        // ---------------------------------------------------------

        // 1. ドラッグ開始 (Source)
        if (ImGui::BeginDragDropSource()) {
            // IDをペイロードとして渡す
            ImGui::SetDragDropPayload("HIERARCHY_ENTITY", &entityID, sizeof(CCL::ECS::EntityID));
            ImGui::Text("%s", name.c_str());
            ImGui::EndDragDropSource();
        }

        // 2. ドロップ受け入れ (Target) -> 親子付け
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("HIERARCHY_ENTITY")) {
                CCL::ECS::EntityID childID = *(const CCL::ECS::EntityID *)payload->Data;

                // 自分自身や、自分の親（循環参照）でなければ親子付けする
                if (childID != entityID) {
                    auto *world      = context.world;
                    auto *childTrans = world->GetComponent<TransformComponent>(childID);
                    if (childTrans) {
                        childTrans->parentID = entityID; // 親を設定！
                    }
                }
            }
            ImGui::EndDragDropTarget();
        }

        // ---------------------------------------------------------
        // 子要素の再帰描画
        // ---------------------------------------------------------
        if (hasChildren && opened) {
            // 子リストを取得して描画
            // (Note: mapへの参照なので安全)
            const auto &children = childrenMap.at(entityID);
            for (auto childID : children) {
                DrawEntityNode(childID, context, childrenMap);
            }
            ImGui::TreePop();
        }

        // 右クリックメニュー (オプション)
        if (ImGui::BeginPopupContextItem()) {
            if (ImGui::MenuItem("Delete Entity")) {
                context.world->Destroy(entityID);
                if (*context.selectedEntity == entityID) *context.selectedEntity = 0;
            }
            ImGui::EndPopup();
        }
    }


private:

    // アーキタイプから「何者か」を推測する論理エンジン
    std::string GetInferredName(const CCL::ECS::Archetype& arch) {
        using namespace CCL::ECS;
        // 判定用のArchetypeをキャッシュ
        static const auto playArch = ArchetypeHelper::Generate<PlayerComponent>();
        
        static const auto dirLightArch = ArchetypeHelper::Generate<DirectionalLightComponent>();
        static const auto poiLightArch = ArchetypeHelper::Generate<PointLightComponent>();
        static const auto spotLightArch = ArchetypeHelper::Generate<SpotLightComponent>();

        if (ArchetypeHelper::LhasR(arch, playArch)) return "Player";
        if (ArchetypeHelper::LhasR(arch, dirLightArch)) return "DirectionalLight";
        if (ArchetypeHelper::LhasR(arch, poiLightArch)) return "PointLight";
        if (ArchetypeHelper::LhasR(arch, spotLightArch)) return "SpotLight";

        return "Empty";
    }
};