/// @file  VirtualCameraComponentMeta.cpp
/// @brief VirtualCamera関連コンポーネントのシリアライズ/インスペクタ定義。

#include "Engine/Camera/Components/VirtualCameraComponents.h"
#include "Engine/Serialization/Meta/ComponentDefine.h"
#include "Engine/Serialization/Meta/ComponentCategories.h"
#include <algorithm> 

#ifdef ENABLE_EDITOR
#include "Editor/Inspector/ComponentGuiRegistry.h"
#include "Engine/Serialization/Meta/ComponentMetaImGui.h"
#endif

// ============================================================================
// Virtual Camera Base 
// BEGIN_FULL を使用し、カテゴリと優先度を明記
// ============================================================================
COMPONENT_META_BEGIN_FULL(VirtualCamera, "VirtualCamera", CCL::Categories::Camera, 300, false)
    CFIELD_INT_RANGED(priority, "優先度", -100, 100, "Camera Settings")
    CFIELD_FLOAT_RANGED(blendTime, "ブレンド時間(秒)", 0.1f, 0.0f, 10.0f, "Camera Settings")
    CFIELD_FLOAT_RANGED(fov, "視野角(FOV)", 0.5f, 1.0f, 179.0f, "Lens")
    CFIELD_FLOAT_RANGED(nearClip, "ニアクリップ", 0.01f, 0.001f, 1000.0f, "Lens")
    CFIELD_FLOAT_RANGED(farClip, "ファークリップ", 10.0f, 1.0f, 100000.0f, "Lens")
COMPONENT_META_END(VirtualCamera)

// ============================================================================
// Camera Body Follow 
// ============================================================================
template <> struct ComponentMeta<CameraBodyFollow> {
    CCL_META_USING_THIS(CameraBodyFollow)
    static constexpr const char* displayName    = "Camera Body (Follow)";
    static constexpr const char* category       = CCL::Categories::Camera;
    static constexpr int         sortPriority   = 310;
    static constexpr bool        hiddenInInspector = false;
    static constexpr bool        hasCustomGui   = true;
    static constexpr bool        isSerializable = true;

#ifdef ENABLE_EDITOR
    static bool CustomGui(CameraBodyFollow& comp, unsigned long long /*entityID*/ = 0, void* /*world*/ = nullptr) {
        bool changed = false;

        ImGui::SeparatorText("ターゲット連携");

        int targetInt = static_cast<int>(comp.target.id);
        if (ImGui::InputInt("ターゲットID", &targetInt)) {
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

        ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "[ ここにHierarchyからエンティティをドロップ ]");

        ImGui::SeparatorText("追従設定");
        changed |= ImGui::DragFloat3("追従オフセット", &comp.offset.x, 0.1f);
        changed |= ImGui::DragFloat("減衰", &comp.damping, 0.1f, 0.0f, 100.0f);
        changed |= ImGui::Checkbox("Y高さを固定", &comp.lockY);

        return changed;
    }
#else
    static bool CustomGui(CameraBodyFollow&, unsigned long long, void*) { return false; }
#endif

    static const std::vector<FieldDescriptor>& Fields() {
        static const std::vector<FieldDescriptor> fields = {
            CFIELD(target, "ターゲット", "Follow Settings")
            CFIELD(offset, "追従オフセット", "Follow Settings")
            CFIELD_FLOAT_RANGED(damping, "減衰", 0.1f, 0.0f, 100.0f, "Follow Settings")
            CFIELD(lockY, "Y高さを固定", "Follow Settings")
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
    static constexpr const char* category       = CCL::Categories::Camera;
    static constexpr int         sortPriority   = 320;
    static constexpr bool        hiddenInInspector = false;
    static constexpr bool        hasCustomGui   = true;
    static constexpr bool        isSerializable = true;

#ifdef ENABLE_EDITOR
    static bool CustomGui(CameraAimLookAt& comp, unsigned long long /*entityID*/ = 0, void* /*world*/ = nullptr) {
        bool changed = false;

        ImGui::SeparatorText("ターゲット連携");

        int targetInt = static_cast<int>(comp.target.id);
        if (ImGui::InputInt("ターゲットID", &targetInt)) {
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
        ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "[ ここにHierarchyからエンティティをドロップ ]");

        ImGui::SeparatorText("注視設定");
        changed |= ImGui::DragFloat3("注視オフセット", &comp.offset.x, 0.1f);
        changed |= ImGui::DragFloat("減衰", &comp.damping, 0.1f, 0.0f, 100.0f);

        return changed;
    }
#else
    static bool CustomGui(CameraAimLookAt&, unsigned long long, void*) { return false; }
#endif

    static const std::vector<FieldDescriptor>& Fields() {
        static const std::vector<FieldDescriptor> fields = {
            CFIELD(target, "ターゲット", "Aim Settings")
            CFIELD(offset, "注視オフセット", "Aim Settings")
            CFIELD_FLOAT_RANGED(damping, "減衰", 0.1f, 0.0f, 100.0f, "Aim Settings")
        };
        return fields;
    }
};
REGISTER_COMPONENT(CameraAimLookAt, "CameraAimLookAt")
REGISTER_COMPONENT_EDITOR_GUI(CameraAimLookAt)

// ============================================================================
// Camera Shake 
// BEGIN_CAT を使用
// ============================================================================
COMPONENT_META_BEGIN_FULL(CameraShake, "Camera Shake", CCL::Categories::Camera, 330, false)
    CFIELD_FLOAT_RANGED(amplitude, "揺れの強さ", 0.1f, 0.0f, 100.0f, "Shake State")
    CFIELD_FLOAT_RANGED(duration, "残り時間", 0.01f, 0.0f, 10.0f, "Shake State")
    CFIELD_FLOAT_RANGED(maxDuration, "最大時間", 0.01f, 0.0f, 10.0f, "Shake State")
COMPONENT_META_END(CameraShake)

// ============================================================================
// Camera Body Free 
// BEGIN_CAT を使用
// ============================================================================
COMPONENT_META_BEGIN_FULL(CameraBodyFree, "CameraBodyFree", CCL::Categories::Camera, 340, false)
    CFIELD_FLOAT_RANGED(moveSpeed, "移動速度", 0.1f, 0.0f, 100.0f, "Settings")
    CFIELD_FLOAT_RANGED(lookSpeed, "視点速度", 0.01f, 0.0f, 10.0f, "Settings")
    CFIELD_FLOAT_RANGED(currentPitch, "現在ピッチ", 0.1f, -89.0f, 89.0f, "State")
    CFIELD_FLOAT_RANGED(currentYaw, "現在ヨー", 0.1f, -360.0f, 360.0f, "State")
COMPONENT_META_END(CameraBodyFree)