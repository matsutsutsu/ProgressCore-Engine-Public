// ============================================================================
// JoltPhysicsComponentMeta.cpp
// ============================================================================

#include "Engine/GamePlay/Physics/Collision/JoltBoxColliderComponent.h"
#include "Engine/GamePlay/Physics/Collision/JoltSphereColliderComponent.h"
#include "Engine/GamePlay/Physics/RigidBody/JoltRigidbodyComponent.h"

#include "Engine/Serialization/ComponentRegistry.h"
#include "Engine/Serialization/Meta/ComponentMeta.h"
#include "Engine/Serialization/Meta/ComponentMetaJson.h"

#include "Editor/Inspector/ComponentGuiRegistry.h"
#include "Engine/Serialization/Meta/ComponentMetaImGui.h"

#include "Engine/Serialization/ComponentSerializers.h"

namespace {
    const char *const MotionTypeNames[]  = {"Static", "Kinematic", "Dynamic"};
    const char *const ObjectLayerNames[] = {"NON_MOVING", "MOVING"};
} // namespace

// ============================================================================
// Box / Sphere Collider (マクロを用いた完全自動化GUI)
// ============================================================================
template <> struct ComponentMeta<JoltBoxColliderComponent> {
    static constexpr bool                      registered  = true;
    static constexpr const char               *displayName = "Jolt Box Collider";
    static const std::vector<FieldDescriptor> &Fields()
    {
        static const std::vector<FieldDescriptor> fields = {META_FIELD_FLOAT3(
            JoltBoxColliderComponent, halfExtent, "halfExtent", "Half Extent", 0.1f, "Box Collider")};
        return fields;
    }
};

template <> struct ComponentMeta<JoltSphereColliderComponent> {
    static constexpr bool                      registered  = true;
    static constexpr const char               *displayName = "Jolt Sphere Collider";
    static const std::vector<FieldDescriptor> &Fields()
    {
        static const std::vector<FieldDescriptor> fields = {META_FIELD_FLOAT(JoltSphereColliderComponent,
            radius,
            "radius",
            "Radius",
            0.1f,
            0.0f,
            100.0f,
            "Sphere Collider")};
        return fields;
    }
};

// ============================================================================
// Rigidbody (マクロを用いた完全自動化GUI)
// ============================================================================
template <> struct ComponentMeta<JoltRigidbodyComponent> {
    static constexpr bool                      registered  = true;
    static constexpr const char               *displayName = "Jolt Rigidbody";
    static const std::vector<FieldDescriptor> &Fields()
    {
        static const std::vector<FieldDescriptor> fields = {
            META_FIELD_ENUM_U8(JoltRigidbodyComponent,
                motionType,
                "motionType",
                "Motion Type",
                MotionTypeNames,
                3,
                "Physics"),
            META_FIELD_ENUM_U8(JoltRigidbodyComponent,
                objectLayer,
                "objectLayer",
                "Object Layer",
                ObjectLayerNames,
                2,
                "Physics"),
            META_FIELD_FLOAT(JoltRigidbodyComponent,
                restitution,
                "restitution",
                "Restitution (反発)",
                0.05f,
                0.0f,
                1.0f,
                "Material"),
            META_FIELD_FLOAT(JoltRigidbodyComponent,
                friction,
                "friction",
                "Friction (摩擦)",
                0.05f,
                0.0f,
                1.0f,
                "Material")};
        return fields;
    }
};

// ============================================================================
// 自動登録システム
// ============================================================================
void RegisterPhysicsMeta()
{
    auto &jsonRegistry = ComponentRegistry::Instance();
    ComponentMetaJson::RegisterMetaT<JoltBoxColliderComponent>(
        jsonRegistry, "JoltBoxCollider");
    ComponentMetaJson::RegisterMetaT<JoltSphereColliderComponent>(
        jsonRegistry, "JoltSphereCollider");
    ComponentMetaJson::RegisterMetaT<JoltRigidbodyComponent>(jsonRegistry, "JoltRigidbody");

    auto &guiRegistry = ComponentGuiRegistry::Instance();
    ComponentMetaImGui::RegisterGuiMeta<JoltBoxColliderComponent>(guiRegistry);
    ComponentMetaImGui::RegisterGuiMeta<JoltSphereColliderComponent>(guiRegistry);
    ComponentMetaImGui::RegisterGuiMeta<JoltRigidbodyComponent>(guiRegistry);
}