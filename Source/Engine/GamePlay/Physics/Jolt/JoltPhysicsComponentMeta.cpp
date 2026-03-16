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
            JoltBoxColliderComponent, halfExtent, "Half Extent", 0.1f, "Dimensions")};
        return fields;
    }
};

template <> struct ComponentMeta<JoltSphereColliderComponent> {
    static constexpr bool                      registered  = true;
    static constexpr const char               *displayName = "Jolt Sphere Collider";
    static const std::vector<FieldDescriptor> &Fields()
    {
        static const std::vector<FieldDescriptor> fields = {META_FIELD_FLOAT(
            JoltSphereColliderComponent, radius, "Radius", 0.1f, 0.01f, 1000.0f, "Dimensions")};
        return fields;
    }
};

// ============================================================================
// Rigidbody (★ カスタムGUIを使用したハイブリッド実装)
// ============================================================================
template <> struct ComponentMeta<JoltRigidbodyComponent> {
    static constexpr bool        registered  = true;
    static constexpr const char *displayName = "Jolt Rigidbody";

    //  1. カスタムGUIを有効化するフラグ
    static constexpr bool hasCustomGui = true;

    //  2. 独自のGUI描画ロジック (hasCustomGui = true の場合のみ呼ばれる)
    static bool CustomGui(JoltRigidbodyComponent &comp)
    {
        bool changed = false;

        // 自由にImGuiを記述可能。色を変えたり、ボタンを配置したりできます。
        ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "[ Custom Physics Settings ]");
        ImGui::Separator();
        ImGui::Spacing();

        // 列挙型のカスタムコンボボックス
        int motionTypeInt = (int)comp.motionType;
        if (ImGui::Combo("Motion Type", &motionTypeInt, MotionTypeNames, 3)) {
            comp.motionType = (JPH::EMotionType)motionTypeInt;
            changed         = true;
        }

        int layerInt = (int)comp.objectLayer;
        if (ImGui::Combo("Object Layer", &layerInt, ObjectLayerNames, 2)) {
            comp.objectLayer = (JPH::ObjectLayer)layerInt;
            changed          = true;
        }

        // スライダー
        changed |= ImGui::SliderFloat("Restitution", &comp.restitution, 0.0f, 1.0f);
        changed |= ImGui::SliderFloat("Friction", &comp.friction, 0.0f, 1.0f);

        // 特定の処理を行うボタン（例：パラメータのリセット）
        ImGui::Spacing();
        if (ImGui::Button("Reset Physics Material", ImVec2(-1, 0))) {
            comp.restitution = 0.0f;
            comp.friction    = 0.2f;
            changed          = true;
        }

        return changed; // 変更があったらtrueを返す
    }

    //  3. JSON保存用・自動生成用のフィールド定義（マクロでスッキリ記述）
    static const std::vector<FieldDescriptor> &Fields()
    {
        static const std::vector<FieldDescriptor> fields = {
            META_FIELD_ENUM_U8(
                JoltRigidbodyComponent, motionType, "Motion Type", MotionTypeNames, 3, "Physics"),
            META_FIELD_ENUM_INT(JoltRigidbodyComponent,
                objectLayer,
                "Object Layer",
                ObjectLayerNames,
                2,
                "Physics"),
            META_FIELD_FLOAT(JoltRigidbodyComponent,
                restitution,
                "Restitution (反発)",
                0.05f,
                0.0f,
                1.0f,
                "Material"),
            META_FIELD_FLOAT(JoltRigidbodyComponent,
                friction,
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