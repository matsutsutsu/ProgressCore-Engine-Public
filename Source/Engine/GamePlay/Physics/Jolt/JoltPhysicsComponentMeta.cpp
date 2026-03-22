// ============================================================================
// JoltPhysicsComponentMeta.cpp
// ============================================================================

#include "Engine/GamePlay/Physics/Collision/JoltBoxColliderComponent.h"
#include "Engine/GamePlay/Physics/Collision/JoltSphereColliderComponent.h"
#include "Engine/GamePlay/Physics/Collision/JoltCapsuleColliderComponent.h" 
#include "Engine/GamePlay/Physics/RigidBody/JoltRigidbodyComponent.h"
// ★追加: CharacterVirtualの設定コンポーネントのインクルード
#include "Engine/GamePlay/Physics/Character/JoltCharacterConfigComponent.h" 

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
// Box / Sphere / Capsule Collider
// ============================================================================
template <> struct ComponentMeta<JoltBoxColliderComponent> {
    static constexpr bool                      registered  = true;
    static constexpr const char               *displayName = "Jolt Box Collider";
    static const std::vector<FieldDescriptor> &Fields()
    {
        static const std::vector<FieldDescriptor> fields = {
            META_FIELD_FLOAT3(JoltBoxColliderComponent, halfExtent, "halfExtent", "Half Extent", 0.1f, "Box Collider"),
            META_FIELD_BOOL(JoltBoxColliderComponent, isDirty, "isDirty", "Apply Size Change (適用)", "Box Collider")
        };
        return fields;
    }
};

template <> struct ComponentMeta<JoltSphereColliderComponent> {
    static constexpr bool                      registered  = true;
    static constexpr const char               *displayName = "Jolt Sphere Collider";
    static const std::vector<FieldDescriptor> &Fields()
    {
        static const std::vector<FieldDescriptor> fields = {
            META_FIELD_FLOAT(JoltSphereColliderComponent, radius, "radius", "Radius", 0.1f, 0.0f, 100.0f, "Sphere Collider"),
            META_FIELD_BOOL(JoltSphereColliderComponent, isDirty, "isDirty", "Apply Size Change (適用)", "Sphere Collider")
        };
        return fields;
    }
};

template <> struct ComponentMeta<JoltCapsuleColliderComponent> {
    static constexpr bool                      registered  = true;
    static constexpr const char               *displayName = "Jolt Capsule Collider";
    static const std::vector<FieldDescriptor> &Fields()
    {
        static const std::vector<FieldDescriptor> fields = {
            META_FIELD_FLOAT(JoltCapsuleColliderComponent, halfHeight, "halfHeight", "Half Height", 0.1f, 0.0f, 100.0f, "Capsule Collider"),
            META_FIELD_FLOAT(JoltCapsuleColliderComponent, radius, "radius", "Radius", 0.1f, 0.0f, 100.0f, "Capsule Collider"),
            META_FIELD_BOOL(JoltCapsuleColliderComponent, isDirty, "isDirty", "Apply Size Change (適用)", "Capsule Collider")
        };
        return fields;
    }
};

// ============================================================================
// Rigidbody (拡張パラメータを追加)
// ============================================================================
template <> struct ComponentMeta<JoltRigidbodyComponent> {
    static constexpr bool                      registered  = true;
    static constexpr const char               *displayName = "Jolt Rigidbody";
    static const std::vector<FieldDescriptor> &Fields()
    {
        static const std::vector<FieldDescriptor> fields = {
            META_FIELD_ENUM_U8(JoltRigidbodyComponent,
                motionType, "motionType", "Motion Type", MotionTypeNames, 3, "Physics"),
            META_FIELD_ENUM_U8(JoltRigidbodyComponent,
                objectLayer, "objectLayer", "Object Layer", ObjectLayerNames, 2, "Physics"),
            META_FIELD_FLOAT(JoltRigidbodyComponent,
                restitution, "restitution", "Restitution (反発)", 0.05f, 0.0f, 1.0f, "Material"),
            META_FIELD_FLOAT(JoltRigidbodyComponent,
                friction, "friction", "Friction (摩擦)", 0.05f, 0.0f, 1.0f, "Material"),
            
            // ★追加: 拡張パラメータ群
            META_FIELD_FLOAT3(JoltRigidbodyComponent,
                initialVelocity, "initialVelocity", "Initial Velocity", 0.5f, "Physics Extended"),
            META_FIELD_FLOAT(JoltRigidbodyComponent,
                gravityFactor, "gravityFactor", "Gravity Factor", 0.1f, -10.0f, 10.0f, "Physics Extended"),
            META_FIELD_BOOL(JoltRigidbodyComponent,
                isSensor, "isSensor", "Is Sensor (すり抜け)", "Physics Extended"),
            META_FIELD_BOOL(JoltRigidbodyComponent,
                useCCD, "useCCD", "Use CCD (壁抜け防止)", "Physics Extended")
        };
        return fields;
    }
};

// ============================================================================
// ★追加: Character Config
// ============================================================================
template <> struct ComponentMeta<JoltCharacterConfigComponent> {
    static constexpr bool                      registered  = true;
    static constexpr const char               *displayName = "Jolt Character Config";
    static const std::vector<FieldDescriptor> &Fields()
    {
        static const std::vector<FieldDescriptor> fields = {
            META_FIELD_FLOAT(JoltCharacterConfigComponent, 
                maxSlopeAngle, "maxSlopeAngle", "Max Slope Angle", 1.0f, 0.0f, 89.0f, "Character Settings"),
            META_FIELD_FLOAT(JoltCharacterConfigComponent, 
                maxStepHeight, "maxStepHeight", "Max Step Height", 0.1f, 0.0f, 5.0f, "Character Settings"),
            META_FIELD_FLOAT(JoltCharacterConfigComponent, 
                walkSpeed, "walkSpeed", "Walk Speed", 0.5f, 0.0f, 100.0f, "Character Settings"),
            META_FIELD_FLOAT(JoltCharacterConfigComponent, 
                jumpSpeed, "jumpSpeed", "Jump Speed", 0.5f, 0.0f, 100.0f, "Character Settings"),
            META_FIELD_FLOAT(JoltCharacterConfigComponent, 
                characterMass, "characterMass", "Character Mass", 1.0f, 0.1f, 1000.0f, "Character Settings")
        };
        return fields;
    }
};


// ============================================================================
// 自動登録システム
// ============================================================================
void RegisterPhysicsMeta()
{
    auto &jsonRegistry = ComponentRegistry::Instance();
    ComponentMetaJson::RegisterMetaT<JoltBoxColliderComponent>(jsonRegistry, "JoltBoxCollider");
    ComponentMetaJson::RegisterMetaT<JoltSphereColliderComponent>(jsonRegistry, "JoltSphereCollider");
    ComponentMetaJson::RegisterMetaT<JoltCapsuleColliderComponent>(jsonRegistry, "JoltCapsuleCollider");
    ComponentMetaJson::RegisterMetaT<JoltRigidbodyComponent>(jsonRegistry, "JoltRigidbody");
    // ★追加: Jsonへの登録
    ComponentMetaJson::RegisterMetaT<JoltCharacterConfigComponent>(jsonRegistry, "JoltCharacterConfig");

    auto &guiRegistry = ComponentGuiRegistry::Instance();
    ComponentMetaImGui::RegisterGuiMeta<JoltBoxColliderComponent>(guiRegistry);
    ComponentMetaImGui::RegisterGuiMeta<JoltSphereColliderComponent>(guiRegistry);
    ComponentMetaImGui::RegisterGuiMeta<JoltCapsuleColliderComponent>(guiRegistry);
    ComponentMetaImGui::RegisterGuiMeta<JoltRigidbodyComponent>(guiRegistry);
    // ★追加: ImGuiインスペクタへの登録
    ComponentMetaImGui::RegisterGuiMeta<JoltCharacterConfigComponent>(guiRegistry);
}