#include "Editor/Windows/InspectorWindow.h"
#include "Editor/Inspector/ComponentGuiRegistry.h"
#include <algorithm>
#include <cstring>
#include <string>
#include <windows.h>

#include "Engine/Graphics/Core/Graphics.h"
#include "Engine/Platform/Dialog.h"
#include "Engine/Serialization/PrefabSerializer.h"
#include "Game/Core/AllComponents.h"

InspectorWindow::InspectorWindow() : EditorWindow("Inspector") {}

void InspectorWindow::DrawContents(EditorContext &context)
{
    // ★ 修正：ポインタではなく実体を直接取得
    CCL::ECS::EntityID     selected = context.selectedEntity;
    CCL::ECS::Core::World *world    = context.world;

    // 1. エンティティ未選択時のガード
    // InvalidEntityID または 0 なら描画しない
    if (selected == CCL::ECS::InvalidEntityID || selected == 0) {
        ImGui::TextColored(ImVec4(1, 1, 0, 1), "Select an Entity to inspect.");
        return;
    }

    // =========================================================
    // Entity メタデータ編集エリア (名前・ID)
    // =========================================================
    auto *nameComp = world->GetComponent<NameComponent>(selected);

    char filenameBuf[256] = "NewPrefab";

    if (nameComp) {
        if (ImGui::InputText("Entity Name", nameComp->name, sizeof(nameComp->name))) {
            context.entityNames[selected] = nameComp->name;
        }
        if (strlen(nameComp->name) > 0) {
            strcpy_s(filenameBuf, sizeof(filenameBuf), nameComp->name);
        }
    }
    else {
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "(No NameComponent)");

        ImGui::SameLine();
        if (ImGui::Button("+ Add Name")) {
            std::string defaultName = "Entity_" + std::to_string(selected);
            world->AddComponent<NameComponent>(selected, NameComponent(defaultName.c_str()));
            context.entityNames[selected] = defaultName;
        }

        auto it = context.entityNames.find(selected);
        if (it != context.entityNames.end()) {
            strcpy_s(filenameBuf, sizeof(filenameBuf), it->second.c_str());
        }
    }

    ImGui::SameLine();
    ImGui::TextDisabled("(ID: %llu)", selected);
    ImGui::Separator();

    // 削除ボタン
    ImGui::SameLine(ImGui::GetWindowWidth() - 120);
    if (ImGui::Button("Destroy Entity", ImVec2(110, 0))) {
        world->Destroy(selected);
        // ★ 修正：選択を無効化する
        context.selectedEntity = CCL::ECS::InvalidEntityID;
        return; // 破棄されたのでこれ以上の描画は中止
    }

    // =========================================================
    // Prefab保存ボタン
    // =========================================================
    ImGui::Spacing();
    if (ImGui::Button("Save as Prefab JSON", ImVec2(-1, 0))) {
        char filename[256] = {0};
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
            PrefabSerializer::Save(filename, world, selected, std::string(filenameBuf));
        }
    }

    ImGui::Separator();

    // =========================================================
    // コンポーネント一覧表示と編集
    // =========================================================
    const auto &archetype = world->GetEntityArchetype(selected);
    auto       &registry  = ComponentGuiRegistry::Instance();

    for (const auto &typeData : archetype) {
        CCL::ECS::TypeID typeID   = typeData.id;
        std::string      compName = registry.GetName(typeID);

        ImGui::PushID((int)typeID);
        bool headerOpen = ImGui::CollapsingHeader(
            compName.c_str(), ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowItemOverlap);

        float buttonSize = 20.0f;
        ImGui::SameLine(ImGui::GetWindowWidth() - buttonSize - 20);
        if (ImGui::Button("x", ImVec2(buttonSize, 18))) {
            world->RequestRemoveComponent(selected, typeID);
        }

        if (headerOpen) {
            ImGui::Indent();
            registry.DrawInspector(typeID, world, selected);
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
            bool alreadyHas = world->HasComponent(selected, typeID);
            if (alreadyHas) ImGui::BeginDisabled();

            if (ImGui::Selectable(registry.GetName(typeID).c_str())) {
                auto &entry = registry.GetEntry(typeID);
                world->RequestAddComponent(selected,
                    typeID,
                    entry.size,
                    entry.ctor,
                    entry.dtor,
                    entry.constructor,
                    entry.assigner,
                    entry.mover);
            }
            if (alreadyHas) ImGui::EndDisabled();
        }
        ImGui::EndPopup();
    }
}