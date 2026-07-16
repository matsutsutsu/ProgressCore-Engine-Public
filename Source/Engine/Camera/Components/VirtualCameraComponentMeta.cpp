/// @file  VirtualCameraComponentMeta.cpp
/// @brief VirtualCamera関連コンポーネントのシリアライズ/インスペクタ定義。

#include "Engine/Camera/Components/VirtualCameraComponents.h"
#include "Engine/Serialization/Meta/ComponentDefine.h"
#include "Engine/Serialization/Meta/ComponentCategories.h" // ★追加
#include <algorithm> 

#ifdef ENABLE_EDITOR
#include "Editor/Inspector/ComponentGuiRegistry.h"
#include "Engine/Serialization/Meta/ComponentMetaImGui.h"
#endif

// ============================================================================
// Virtual Camera Base 
// ★修正: BEGIN_FULL を使用し、カテゴリと優先度を明記
// ============================================================================
COMPONENT_META_BEGIN_FULL(VirtualCamera, "VirtualCamera", CCL::Categories::Camera, 300, false)
    CFIELD_INT_RANGED(priority, "Priority", -100, 100, "Camera Settings")
    CFIELD_FLOAT_RANGED(blendTime, "Blend Time (s)", 0.1f, 0.0f, 10.0f, "Camera Settings")
    CFIELD_FLOAT_RANGED(fov, "Field of View (FOV)", 0.5f, 1.0f, 179.0f, "Lens")
    CFIELD_FLOAT_RANGED(nearClip, "Near Clip", 0.01f, 0.001f, 1000.0f, "Lens")
    CFIELD_FLOAT_RANGED(farClip, "Far Clip", 10.0f, 1.0f, 100000.0f, "Lens")
COMPONENT_META_END(VirtualCamera)

// ============================================================================
// Camera Body Follow 
// ============================================================================
template <> struct ComponentMeta<CameraBodyFollow> {
    CCL_META_USING_THIS(CameraBodyFollow)
    static constexpr const char* displayName    = "Camera Body (Follow)";
    static constexpr const char* category       = CCL::Categories::Camera; // ★追加
    static constexpr int         sortPriority   = 310; // ★追加
    static constexpr bool        hiddenInInspector = false;
    static constexpr bool        hasCustomGui   = true;
    static constexpr bool        isSerializable = true;

#ifdef ENABLE_EDITOR
    static bool CustomGui(CameraBodyFollow& comp, unsigned long long /*entityID*/ = 0, void* /*world*/ = nullptr) {
        bool changed = false;

        ImGui::SeparatorText("Target Link");

        int targetInt = static_cast<int>(comp.target.id);
        if (ImGui::InputInt("Target ID", &targetInt)) {
            comp.target.id = static_cast<unsigned long long>((std::max)(0, targetInt));
            changed = true;
        }

        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("DND_ENTITY_ID")) {
                unsigned long long droppedID = *(const unsigned long long*)payload->Data;
                comp.target.id = droppedID; 
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
// Camera Aim LookAt 
// ============================================================================
template <> struct ComponentMeta<CameraAimLookAt> {
    CCL_META_USING_THIS(CameraAimLookAt)
    static constexpr const char* displayName    = "Camera Aim (LookAt)";
    static constexpr const char* category       = CCL::Categories::Camera; // ★追加
    static constexpr int         sortPriority   = 320; // ★追加
    static constexpr bool        hiddenInInspector = false;
    static constexpr bool        hasCustomGui   = true;
    static constexpr bool        isSerializable = true;

#ifdef ENABLE_EDITOR
    static bool CustomGui(CameraAimLookAt& comp, unsigned long long /*entityID*/ = 0, void* /*world*/ = nullptr) {
        bool changed = false;

        ImGui::SeparatorText("Target Link");

        int targetInt = static_cast<int>(comp.target.id);
        if (ImGui::InputInt("Target ID", &targetInt)) {
            comp.target.id = static_cast<unsigned long long>((std::max)(0, targetInt));
            changed = true;
        }

        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("DND_ENTITY_ID")) {
                unsigned long long droppedID = *(const unsigned long long*)payload->Data;
                comp.target.id = droppedID; 
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
// Camera Shake 
// ★修正: BEGIN_CAT を使用
// ============================================================================
COMPONENT_META_BEGIN_FULL(CameraShake, "Camera Shake", CCL::Categories::Camera, 330, false)
    CFIELD_FLOAT_RANGED(amplitude, u8"揺れの強さ (amplitude)", 0.1f, 0.0f, 100.0f, "Shake State")
    CFIELD_FLOAT_RANGED(duration, u8"残り時間 (duration)", 0.01f, 0.0f, 10.0f, "Shake State")
    CFIELD_FLOAT_RANGED(maxDuration, u8"最大時間 (maxDuration)", 0.01f, 0.0f, 10.0f, "Shake State")
COMPONENT_META_END(CameraShake)

// ============================================================================
// Camera Body Free 
// ★修正: BEGIN_CAT を使用
// ============================================================================
COMPONENT_META_BEGIN_FULL(CameraBodyFree, "CameraBodyFree", CCL::Categories::Camera, 340, false)
    CFIELD_FLOAT_RANGED(moveSpeed, "Move Speed", 0.1f, 0.0f, 100.0f, "Settings")
    CFIELD_FLOAT_RANGED(lookSpeed, "Look Speed", 0.01f, 0.0f, 10.0f, "Settings")
    CFIELD_FLOAT_RANGED(currentPitch, "Current Pitch", 0.1f, -89.0f, 89.0f, "State")
    CFIELD_FLOAT_RANGED(currentYaw, "Current Yaw", 0.1f, -360.0f, 360.0f, "State")
COMPONENT_META_END(CameraBodyFree)