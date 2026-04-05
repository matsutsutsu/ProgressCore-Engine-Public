#include "PlayerCarComponent.h"
#include "Editor/Inspector/ComponentGuiRegistry.h"
#include "Engine/Serialization/ComponentRegistry.h"
#include "Engine/Serialization/Meta/ComponentMeta.h"
#include "Engine/Serialization/Meta/ComponentMetaImGui.h"
#include "Engine/Serialization/Meta/ComponentMetaJson.h"
#include <imgui.h>

// ============================================================================
// Player Car Controller
// ============================================================================
template <> struct ComponentMeta<PlayerCarComponent> {
    static constexpr bool        registered = true;
    static constexpr const char* displayName = "Player Car Controller";
    static constexpr bool        hasCustomGui = true; // ★カスタムGUIを有効化

    // 1. 自動シリアライズ対象のフィールド定義
    static const std::vector<FieldDescriptor>& Fields()
    {
        static const std::vector<FieldDescriptor> fields = {
            META_FIELD_FLOAT(PlayerCarComponent, acceleration, "acceleration", "Acceleration", 50.0f, 0.0f, 20000.0f, "Settings"),
            META_FIELD_FLOAT(PlayerCarComponent, turnSpeed, "turnSpeed", "Turn Speed", 1.0f, 0.0f, 1000.0f, "Settings")
        };
        return fields;
    }

    // 2. カスタムGUIによる「ドラッグ＆ドロップ」の実装
    static bool CustomGui(PlayerCarComponent& comp, unsigned long long entityID = 0, void* world = nullptr)
    {
        bool changed = false;

        // 標準フィールドの描画 (acceleration, turnSpeed)
        for (const auto& fd : Fields()) {
            if (ComponentMetaImGui::DrawField(fd, &comp)) changed = true;
        }

        ImGui::Separator();
        ImGui::Text("Physics Link");

        // --- EntityIDのドラッグ＆ドロップ受け入れ処理 ---
        // 現在リンクされているEntityの名前などを表示（デバッグ用）
        std::string label = (comp.physicsSphereID == 0) ? "(None)" : "Linked Sphere ID: " + std::to_string(comp.physicsSphereID);

        ImGui::Button(label.c_str(), ImVec2(-1, 0));

        // ★ドラッグ＆ドロップの受け入れ（他のEntityをここにドロップできる）
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("DND_ENTITY_ID")) {
                unsigned long long droppedID = *(const unsigned long long*)payload->Data;
                comp.physicsSphereID = droppedID;
                changed = true;
            }
            ImGui::EndDragDropTarget();
        }

        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("ここに物理球体(Sphere)のEntityをドラッグしてください");
        }

        return changed;
    }
};

REGISTER_COMPONENT(PlayerCarComponent, "PlayerCar")