#pragma once
#include "Editor/Core/EditorWindow.h"
#include "Editor/Inspector/ComponentGuiRegistry.h"
#include <algorithm>
#include <cstring> // strcpy_s, sprintf_s用

// NameComponent 等を知るために必要
#include "Game/Core/AllComponents.h"

#include "Engine/Serialization/PrefabSerializer.h" // 作成した保存機能
#include "Engine/Graphics/Core/Graphics.h"             // HWND取得用
#include "Engine/Platform/Dialog.h"                  // ダイアログ用

class InspectorWindow : public EditorWindow {
  public:
    InspectorWindow() : EditorWindow("Inspector") {}

  protected:
    void DrawContents(EditorContext &context) override
    {
        CCL::ECS::EntityID    *selected = context.selectedEntity;
        CCL::ECS::Core::World *world    = context.world;

        // 1. エンティティ未選択時のガード
        if (selected == nullptr || *selected == 0) {
            ImGui::TextColored(ImVec4(1, 1, 0, 1), "Select an Entity to inspect.");
            return;
        }

        // =========================================================
        // Entity メタデータ編集エリア (名前・ID)
        // =========================================================

        // ★修正: NameComponentの状態を「ありのまま」に扱う
        auto *nameComp = world->GetComponent<NameComponent>(*selected);

        // 保存用ファイル名の準備
        char filenameBuf[256] = "NewPrefab";

        if (nameComp) {
            // A. 名前がある場合: 素直に編集させる
            // InputTextFlags_EnterReturnsTrue は外して、編集終了時に確定させる方が自然です
            if (ImGui::InputText("Entity Name", nameComp->name, sizeof(nameComp->name))) {
                // 編集中の毎フレーム更新（Hierarchy同期のため）
                context.entityNames[*selected] = nameComp->name;
            }
            // 保存用に名前をコピー
            if (strlen(nameComp->name) > 0) {
                strcpy_s(filenameBuf, sizeof(filenameBuf), nameComp->name);
            }
        }
        else {
            // B. 名前がない場合: 正直に「ない」と言う
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "(No NameComponent)");

            ImGui::SameLine();
            if (ImGui::Button("+ Add Name")) {
                // ここで明示的にコンポーネントを追加
                // デフォルト名は "Entity_ID" とかにすると親切
                std::string defaultName = "Entity_" + std::to_string(*selected);
                world->AddComponent<NameComponent>(*selected, NameComponent(defaultName.c_str()));

                // 即座にキャッシュ更新
                context.entityNames[*selected] = defaultName;
            }

            // 既存のキャッシュがあればそれをファイル名候補に使う（親切心）
            auto it = context.entityNames.find(*selected);
            if (it != context.entityNames.end()) {
                strcpy_s(filenameBuf, sizeof(filenameBuf), it->second.c_str());
            }
        }

        ImGui::SameLine();
        ImGui::TextDisabled("(ID: %llu)", *selected);

        ImGui::Separator();

        // 削除ボタン
        ImGui::SameLine(ImGui::GetWindowWidth() - 120);
        if (ImGui::Button("Destroy Entity", ImVec2(110, 0))) {
            world->Destroy(*selected);
            *selected = 0; // 選択解除
        }

        // =========================================================
        // Prefab保存ボタン
        // =========================================================
        ImGui::Spacing();
        if (ImGui::Button("Save as Prefab JSON", ImVec2(-1, 0))) {

            // 保存ダイアログを開く
            char filename[256] = {0};

            // デフォルトファイル名としてEntity名を入れる
            // ★ここで nameBuf が有効になっている必要がある
            if (strlen(filenameBuf) > 0) {
                sprintf_s(filename, "%s.json", filenameBuf);
            }
            else {
                strcpy_s(filename, "NewPrefab.json");
            }

            HWND               hWnd   = Graphics::Instance().GetWindowHandle();
            static const char *filter = "JSON Files (*.json)\0*.json\0All Files (*.*)\0*.*\0";

            if (Dialog::SaveFileName(filename, 256, filter, "Save Prefab", "json", hWnd) ==
                DialogResult::OK) {
                // PrefabSerializerを使って保存
                PrefabSerializer::Save(filename, world, *selected, std::string(filenameBuf));
            }
        }

        ImGui::Separator();

        // =========================================================
        // コンポーネント一覧表示と編集
        // =========================================================
        // 1. Chunkを直接触らず、Worldから「今のコンポーネント構成」をもらう
        const auto& archetype = world->GetEntityArchetype(*selected);
        auto& registry = context.guiRegistry;

        // 2. コンポーネントリストを描画
        for (const auto &typeData : archetype) {
            CCL::ECS::TypeID typeID = typeData.id;
            std::string compName = registry.GetName(typeID);

            ImGui::PushID((int)typeID);
            bool headerOpen = ImGui::CollapsingHeader(compName.c_str(), ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowItemOverlap);

            // 削除ボタン
            float buttonSize = 20.0f;
            ImGui::SameLine(ImGui::GetWindowWidth() - buttonSize - 20);
            if (ImGui::Button("x", ImVec2(buttonSize, 18))) {
                world->RequestRemoveComponent(*selected, typeID);
            }

            if (headerOpen) {
                ImGui::Indent();
                // ★ここが修正点: ポインタ計算を排除し、Registryに委譲
                registry.DrawInspector(typeID, world, *selected);
                ImGui::Unindent();
                ImGui::Spacing();
            }
            ImGui::PopID();
        }

        ImGui::Separator();
        ImGui::Spacing();

        // --- 新規コンポーネント追加メニュー ---
        if (ImGui::Button("+ Add Component", ImVec2(-1, 30))) {
            ImGui::OpenPopup("AddComponentMenu");
        }

        if (ImGui::BeginPopup("AddComponentMenu")) {
            auto allAvailableTypes = registry.GetAllTypeIDs();

            for (auto typeID : allAvailableTypes) {
                // ★修正: Chunk->HasComponent ではなく World->HasComponent を使用
                bool alreadyHas = world->HasComponent(*selected, typeID);

                if (alreadyHas) ImGui::BeginDisabled();

                if (ImGui::Selectable(registry.GetName(typeID).c_str())) {
                    auto &entry = registry.GetEntry(typeID);
                    world->RequestAddComponent(*selected,
                        typeID,
                        entry.size,
                        entry.ctor,
                        entry.dtor,
                        entry.constructor, 
                        entry.assigner,    
                        entry.mover       
                    );
                }

                if (alreadyHas) ImGui::EndDisabled();
            }
            ImGui::EndPopup();
        }
    }
};