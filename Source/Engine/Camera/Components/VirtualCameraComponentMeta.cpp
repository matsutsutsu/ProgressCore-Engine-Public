/// @file  VirtualCameraComponentMeta.cpp
/// @brief VirtualCamera関連コンポーネントのシリアライズ/インスペクタ定義。

#include "Engine/Camera/Components/VirtualCameraComponents.h"
#include "Engine/Serialization/Meta/ComponentDefine.h"
#include <algorithm> // std::max用

#ifdef ENABLE_EDITOR
#include "Editor/Inspector/ComponentGuiRegistry.h"
#include "Engine/Serialization/Meta/ComponentMetaImGui.h"
#endif

// ============================================================================
// Virtual Camera Base (パターンA: 全自動)
// ============================================================================
COMPONENT_META_BEGIN(VirtualCamera, "VirtualCamera")
CFIELD_INT_RANGED(priority, "Priority", -100, 100, "Camera Settings")
CFIELD_FLOAT_RANGED(blendTime, "Blend Time (s)", 0.1f, 0.0f, 10.0f, "Camera Settings")
CFIELD_FLOAT_RANGED(fov, "Field of View (FOV)", 0.5f, 1.0f, 179.0f, "Lens")
CFIELD_FLOAT_RANGED(nearClip, "Near Clip", 0.01f, 0.001f, 1000.0f, "Lens")
CFIELD_FLOAT_RANGED(farClip, "Far Clip", 10.0f, 1.0f, 100000.0f, "Lens")
COMPONENT_META_END(VirtualCamera)

// ============================================================================
// Camera Body Follow (パターンB: カスタムGUI)
// ============================================================================
template <> struct ComponentMeta<CameraBodyFollow> {
    CCL_META_USING_THIS(CameraBodyFollow)
        static constexpr const char* displayName = "Camera Body (Follow)";
    static constexpr bool        hasCustomGui = true;
    static constexpr bool        isSerializable = true;

#ifdef ENABLE_EDITOR
    static bool CustomGui(CameraBodyFollow& comp, unsigned long long /*entityID*/ = 0, void* /*world*/ = nullptr) {
        bool changed = false;

        ImGui::SeparatorText("Target Link");

        // ★ EntityLink の .id へアクセスするよう修正
        int targetInt = static_cast<int>(comp.target.id);
        if (ImGui::InputInt("Target ID", &targetInt)) {
            comp.target.id = static_cast<unsigned long long>((std::max)(0, targetInt));
            changed = true;
        }

        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("DND_ENTITY_ID")) {
                unsigned long long droppedID = *(const unsigned long long*)payload->Data;
                comp.target.id = droppedID; // ★ 修正
                changed = true;
            }
            ImGui::EndDragDropTarget();
        }

        ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "[ Drop Entity Here from Hierarchy ]");

        ImGui::SeparatorText("Follow Settings");
        changed |= ImGui::DragFloat3("Follow Offset", &comp.offset.x, 0.1f);
        changed |= ImGui::DragFloat("Damping", &comp.damping, 0.1f, 0.0f, 100.0f);
        changed |= ImGui::Checkbox("Lock Y Height", &comp.lockY);

        return changed;
    }
#else
    static bool CustomGui(CameraBodyFollow&, unsigned long long, void*) { return false; }
#endif

    static const std::vector<FieldDescriptor>& Fields() {
        static const std::vector<FieldDescriptor> fields = {
            CFIELD(target, "Target Entity ID", "Follow Settings")
            CFIELD(offset, "Follow Offset", "Follow Settings")
            CFIELD_FLOAT_RANGED(damping, "Damping", 0.1f, 0.0f, 100.0f, "Follow Settings")
            CFIELD(lockY, "Lock Y Height", "Follow Settings")
        };
        return fields;
    }
};
REGISTER_COMPONENT(CameraBodyFollow, "CameraBodyFollow")
REGISTER_COMPONENT_EDITOR_GUI(CameraBodyFollow)

// ============================================================================
// Camera Aim LookAt (パターンB: カスタムGUI)
// ============================================================================
template <> struct ComponentMeta<CameraAimLookAt> {
    CCL_META_USING_THIS(CameraAimLookAt)
        static constexpr const char* displayName = "Camera Aim (LookAt)";
    static constexpr bool        hasCustomGui = true;
    static constexpr bool        isSerializable = true;

#ifdef ENABLE_EDITOR
    static bool CustomGui(CameraAimLookAt& comp, unsigned long long /*entityID*/ = 0, void* /*world*/ = nullptr) {
        bool changed = false;

        ImGui::SeparatorText("Target Link");

        // ★ 修正
        int targetInt = static_cast<int>(comp.target.id);
        if (ImGui::InputInt("Target ID", &targetInt)) {
            comp.target.id = static_cast<unsigned long long>((std::max)(0, targetInt));
            changed = true;
        }

        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("DND_ENTITY_ID")) {
                unsigned long long droppedID = *(const unsigned long long*)payload->Data;
                comp.target.id = droppedID; // ★ 修正
                changed = true;
            }
            ImGui::EndDragDropTarget();
        }
        ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "[ Drop Entity Here from Hierarchy ]");

        ImGui::SeparatorText("Aim Settings");
        changed |= ImGui::DragFloat3("LookAt Offset", &comp.offset.x, 0.1f);
        changed |= ImGui::DragFloat("Damping", &comp.damping, 0.1f, 0.0f, 100.0f);

        return changed;
    }
#else
    static bool CustomGui(CameraAimLookAt&, unsigned long long, void*) { return false; }
#endif

    static const std::vector<FieldDescriptor>& Fields() {
        static const std::vector<FieldDescriptor> fields = {
            CFIELD(target, "Target Entity ID", "Aim Settings")
            CFIELD(offset, "LookAt Offset", "Aim Settings")
            CFIELD_FLOAT_RANGED(damping, "Damping", 0.1f, 0.0f, 100.0f, "Aim Settings")
        };
        return fields;
    }
};
REGISTER_COMPONENT(CameraAimLookAt, "CameraAimLookAt")
REGISTER_COMPONENT_EDITOR_GUI(CameraAimLookAt)

// ============================================================================
// Camera Body Free (パターンA: 全自動)
// ============================================================================
COMPONENT_META_BEGIN(CameraBodyFree, "CameraBodyFree")
CFIELD_FLOAT_RANGED(moveSpeed, "Move Speed", 0.1f, 0.0f, 100.0f, "Settings")
CFIELD_FLOAT_RANGED(lookSpeed, "Look Speed", 0.01f, 0.0f, 10.0f, "Settings")
CFIELD_FLOAT_RANGED(currentPitch, "Current Pitch", 0.1f, -89.0f, 89.0f, "State")
CFIELD_FLOAT_RANGED(currentYaw, "Current Yaw", 0.1f, -360.0f, 360.0f, "State")
COMPONENT_META_END(CameraBodyFree)

// ============================================================================
// Camera Body TPS (パターンB: カスタムGUI)
// ============================================================================
template <> struct ComponentMeta<CameraBodyTPS> {
    CCL_META_USING_THIS(CameraBodyTPS)
        static constexpr const char* displayName = "Camera Body (TPS)";
    static constexpr bool        hasCustomGui = true;
    static constexpr bool        isSerializable = true;

#ifdef ENABLE_EDITOR
    static bool CustomGui(CameraBodyTPS& comp, unsigned long long /*entityID*/ = 0, void* /*world*/ = nullptr) {
        bool changed = false;

        for (const auto& fd : Fields()) {
            if (ComponentMetaImGui::DrawField(fd, &comp)) changed = true;
        }

        ImGui::Separator();
        ImGui::Text("Follow Target Link");

        // ★ 修正
        std::string label = (comp.targetEntity.id == 0) ? "(None)" : "Linked Target ID: " + std::to_string(comp.targetEntity.id);
        ImGui::Button(label.c_str(), ImVec2(-1, 0));

        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("DND_ENTITY_ID")) {
                comp.targetEntity.id = *(const unsigned long long*)payload->Data; // ★ 修正
                changed = true;
            }
            ImGui::EndDragDropTarget();
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("追従するEntityをここにドラッグ");

        return changed;
    }
#else
    static bool CustomGui(CameraBodyTPS&, unsigned long long, void*) { return false; }
#endif

    static const std::vector<FieldDescriptor>& Fields() {
        static const std::vector<FieldDescriptor> fields = {
            CFIELD_FLOAT_RANGED(distance, "Distance", 0.1f, 1.0f, 100.0f, "TPS Settings")
            CFIELD_FLOAT_RANGED(currentPitch, "Pitch", 1.0f, -85.0f, 85.0f, "TPS Settings")
            CFIELD_FLOAT_RANGED(currentYaw, "Yaw", 1.0f, -360.0f, 360.0f, "TPS Settings")
            CFIELD_FLOAT_RANGED(lookSpeedX, "Look Speed X", 1.0f, 0.0f, 500.0f, "TPS Settings")
            CFIELD_FLOAT_RANGED(lookSpeedY, "Look Speed Y", 1.0f, 0.0f, 500.0f, "TPS Settings")
            CFIELD(targetOffset, "Target Offset", "TPS Settings")
            CFIELD(targetEntity, "Target Entity ID", "TPS Settings") // ★ CFIELDでEntityLink型と自動認識される
        };
        return fields;
    }
};
REGISTER_COMPONENT(CameraBodyTPS, "CameraBodyTPS")
REGISTER_COMPONENT_EDITOR_GUI(CameraBodyTPS)