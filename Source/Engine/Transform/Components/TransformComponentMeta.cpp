/// @file  TransformComponentMeta.cpp
/// @brief TransformComponent のシリアライズ/インスペクタ定義。
///        位置・回転・スケール・静的フラグ・親 (EntityLink) を扱う。

#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/Serialization/Meta/ComponentDefine.h"
#include <cmath>
#include <algorithm>

#ifdef ENABLE_EDITOR
#include "Editor/Inspector/ComponentGuiRegistry.h"
#include "Engine/Serialization/Meta/ComponentMetaImGui.h"
#endif

namespace {
    DirectX::XMFLOAT3 QuatToEulerDegrees(const DirectX::XMFLOAT4& q) {
        float pitch = std::asin(std::clamp(2.0f * (q.w * q.x - q.y * q.z), -1.0f, 1.0f));
        float yaw = std::atan2(2.0f * (q.w * q.y + q.z * q.x), 1.0f - 2.0f * (q.x * q.x + q.y * q.y));
        float roll = std::atan2(2.0f * (q.w * q.z + q.x * q.y), 1.0f - 2.0f * (q.x * q.x + q.z * q.z));
        return { DirectX::XMConvertToDegrees(pitch), DirectX::XMConvertToDegrees(yaw), DirectX::XMConvertToDegrees(roll) };
    }
    DirectX::XMFLOAT4 EulerDegreesToQuat(const DirectX::XMFLOAT3& euler) {
        DirectX::XMVECTOR q = DirectX::XMQuaternionRotationRollPitchYaw(
            DirectX::XMConvertToRadians(euler.x),
            DirectX::XMConvertToRadians(euler.y),
            DirectX::XMConvertToRadians(euler.z));
        DirectX::XMFLOAT4 out; DirectX::XMStoreFloat4(&out, q); return out;
    }
}

template <> struct ComponentMeta<TransformComponent> {
    CCL_META_USING_THIS(TransformComponent)   // ★ Fields() 内で CFIELD を使うために必須
        static constexpr const char* displayName = "Transform";
    static constexpr bool        hasCustomGui = true;
    static constexpr bool        isSerializable = true;

#ifdef ENABLE_EDITOR
    static bool CustomGui(TransformComponent& comp,
        unsigned long long /*entityID*/ = 0, void* /*worldPtr*/ = nullptr)
    {
        bool changed = false;

        if (ImGui::DragFloat3("Position", &comp.position.x, 0.1f)) {
            comp.isTeleported = true; comp.isStatic = false; changed = true;
        }

        DirectX::XMFLOAT3 euler = QuatToEulerDegrees(comp.rotation);
        if (ImGui::DragFloat3("Rotation", &euler.x, 1.0f)) {
            comp.rotation = EulerDegreesToQuat(euler);
            comp.isTeleported = true; comp.isStatic = false; changed = true;
        }

        if (ImGui::DragFloat3("Scale", &comp.scale.x, 0.1f)) {
            comp.isStatic = false; changed = true;
        }

        ImGui::Spacing();
        changed |= ImGui::Checkbox("Is Static", &comp.isStatic);
        return changed;
    }
#else
    static bool CustomGui(TransformComponent&, unsigned long long, void*) { return false; }
#endif

    /**
     * @brief シリアライズ対象フィールド。
     *   保存する:   position, rotation, scale, isStatic, parent (EntityLink)
     *   保存しない: isDirty / isTeleported / worldMatrix (実行時キャッシュ),
     *               firstChildID / prevSiblingID / nextSiblingID (HierarchySystem 管理)
     *
     *   parent は EntityLink なので、保存時に SaveContext でローカルインデックスへ、
     *   復元時に LoadContext で新 EntityID へ自動変換される (専用コード不要)。
     *
     *   ※ hasCustomGui = true のため、ここの drag speed はインスペクタには影響しない
     *     (インスペクタは上の CustomGui が描画する)。Fields() は純粋に保存定義。
     */
    static const std::vector<FieldDescriptor>& Fields() {
        static const std::vector<FieldDescriptor> fields = {
            CFIELD(position, "Position", "Transform")
            CFIELD(rotation, "Rotation", "Transform")
            CFIELD(scale,    "Scale",    "Transform")
            CFIELD(isStatic, "Is Static","Transform")
            CFIELD(parent,   "Parent",   "Hierarchy")
        };
        return fields;
    }
};

REGISTER_COMPONENT(TransformComponent, "Transform")
REGISTER_COMPONENT_EDITOR_GUI(TransformComponent)