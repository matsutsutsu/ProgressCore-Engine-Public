#include "Editor/Inspector/ComponentGuiRegistry.h"
#include "Engine/Serialization/ComponentRegistry.h"
#include "Engine/Serialization/Meta/ComponentMeta.h"
#include "Engine/Serialization/Meta/ComponentMetaImGui.h"
#include "Engine/Serialization/Meta/ComponentMetaJson.h"
#include "Engine/GamePlay/Camera/VirtualCameraComponents.h"

#include "Engine/Serialization/ComponentSerializers.h"

// ============================================================================
// Virtual Camera Base
// ============================================================================
template <> struct ComponentMeta<VirtualCamera> {
    static constexpr bool        registered  = true;
    static constexpr const char *displayName = "Virtual Camera";

    static const std::vector<FieldDescriptor> &Fields()
    {
        static const std::vector<FieldDescriptor> fields = {
            META_FIELD_INT(VirtualCamera, priority, "Priority", -100, 100, "Camera Settings"),
            META_FIELD_FLOAT(
                VirtualCamera, blendTime, "Blend Time (s)", 0.1f, 0.0f, 10.0f, "Camera Settings"),
            META_FIELD_FLOAT(VirtualCamera, fov, "Field of View (FOV)", 0.5f, 1.0f, 179.0f, "Lens"),
            META_FIELD_FLOAT(VirtualCamera, nearClip, "Near Clip", 0.01f, 0.001f, 1000.0f, "Lens"),
            META_FIELD_FLOAT(VirtualCamera, farClip, "Far Clip", 10.0f, 1.0f, 100000.0f, "Lens")
            // resultPos などの内部状態はシリアライズ（保存）もGUI表示も不要なのでマクロに書かない
        };
        return fields;
    }
};

// ============================================================================
// Camera Body Follow
// ============================================================================
template <> struct ComponentMeta<CameraBodyFollow> {
    static constexpr bool        registered  = true;
    static constexpr const char *displayName = "Camera Body (Follow)";

    static const std::vector<FieldDescriptor> &Fields()
    {
        static const std::vector<FieldDescriptor> fields = {// Target(EntityID)は一旦Intとして扱う
            META_FIELD_INT(
                CameraBodyFollow, target, "Target Entity ID", 0, 999999, "Follow Settings"),
            META_FIELD_FLOAT3(CameraBodyFollow, offset, "Follow Offset", 0.1f, "Follow Settings"),
            META_FIELD_FLOAT(
                CameraBodyFollow, damping, "Damping (遅延)", 0.1f, 0.0f, 100.0f, "Follow Settings"),
            META_FIELD_BOOL(CameraBodyFollow, lockY, "Lock Y Height", "Follow Settings")};
        return fields;
    }
};

// ============================================================================
// Camera Aim LookAt
// ============================================================================
template <> struct ComponentMeta<CameraAimLookAt> {
    static constexpr bool        registered  = true;
    static constexpr const char *displayName = "Camera Aim (LookAt)";

    static const std::vector<FieldDescriptor> &Fields()
    {
        static const std::vector<FieldDescriptor> fields = {
            META_FIELD_INT(CameraAimLookAt, target, "Target Entity ID", 0, 999999, "Aim Settings"),
            META_FIELD_FLOAT3(CameraAimLookAt, offset, "LookAt Offset", 0.1f, "Aim Settings"),
            META_FIELD_FLOAT(
                CameraAimLookAt, damping, "Damping (遅延)", 0.1f, 0.0f, 100.0f, "Aim Settings")};
        return fields;
    }
};


void RegisterCameraMeta()
{
    auto &jsonReg = ComponentRegistry::Instance();
    ComponentMetaJson::RegisterMetaT<VirtualCamera>(jsonReg, "VirtualCamera");
    ComponentMetaJson::RegisterMetaT<CameraBodyFollow>(jsonReg, "CameraBodyFollow");
    ComponentMetaJson::RegisterMetaT<CameraAimLookAt>(jsonReg, "CameraAimLookAt");
    auto &guiReg = ComponentGuiRegistry::Instance();
    ComponentMetaImGui::RegisterGuiMeta<VirtualCamera>(guiReg);
    ComponentMetaImGui::RegisterGuiMeta<CameraBodyFollow>(guiReg);
    ComponentMetaImGui::RegisterGuiMeta<CameraAimLookAt>(guiReg);
}
