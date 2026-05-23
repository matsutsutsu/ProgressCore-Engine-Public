#include "Engine/GamePlay/Transform/TransformComponent.h"
#include "Engine/Serialization/ComponentRegistry.h"
#include "Engine/Serialization/ComponentRegistry_Editor.h"
#include "Engine/Serialization/Meta/ComponentMeta.h"
#include "Engine/Serialization/Meta/ComponentMetaJson.h"

// ImGui は WITH_EDITOR の場合のみ引く
#ifdef WITH_EDITOR
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
        DirectX::XMFLOAT4 out;
        DirectX::XMStoreFloat4(&out, q);
        return out;
    }
}

template <> struct ComponentMeta<TransformComponent> {
    static constexpr bool        registered = true;
    static constexpr const char* displayName = "Transform";
    static constexpr bool        hasCustomGui = true;

#ifdef WITH_EDITOR
    static bool CustomGui(TransformComponent& comp,
        unsigned long long /*entityID*/ = 0,
        void* /*worldPtr*/ = nullptr)
    {
        bool changed = false;

        if (ImGui::DragFloat3("Position", &comp.position.x, 0.1f)) {
            comp.isTeleported = true;
            comp.isStatic = false;
            changed = true;
        }

        DirectX::XMFLOAT3 euler = QuatToEulerDegrees(comp.rotation);
        if (ImGui::DragFloat3("Rotation", &euler.x, 1.0f)) {
            comp.rotation = EulerDegreesToQuat(euler);
            comp.isTeleported = true;
            comp.isStatic = false;
            changed = true;
        }

        if (ImGui::DragFloat3("Scale", &comp.scale.x, 0.1f)) {
            comp.isStatic = false;
            changed = true;
        }

        ImGui::Spacing();
        changed |= ImGui::Checkbox("Is Static", &comp.isStatic);

        return changed;
    }
#else
    static bool CustomGui(TransformComponent&, unsigned long long, void*) { return false; }
#endif

    /**
     * @brief シリアライズするフィールド一覧。
     *
     * 保存するフィールド:
     *   position, rotation, scale, isStatic
     *   parent (EntityLink) ← ★ シリアライザが自動でローカルインデックス変換
     *
     * 保存しないフィールド:
     *   isDirty, isTeleported, worldMatrix : 実行時計算キャッシュ
     *   firstChildID, prevSiblingID, nextSiblingID : HierarchySystem が書くランタイム値
     *
     * @note parentID を EntityLink に変更したので META_FIELD_ENTITY_LINK を使う。
     *   これにより SceneSerializer / PrefabSerializer に
     *   「Transform の parent を特別扱いするコード」は一切不要。
     */
    static const std::vector<FieldDescriptor>& Fields()
    {
        static const std::vector<FieldDescriptor> fields = {
            META_FIELD_FLOAT3(TransformComponent, position, "position", "Position", 0.1f, "Transform"),
            META_FIELD_FLOAT4(TransformComponent, rotation, "rotation", "Rotation", 0.1f, "Transform"),
            META_FIELD_FLOAT3(TransformComponent, scale,    "scale",    "Scale",    0.1f, "Transform"),
            META_FIELD_BOOL(TransformComponent, isStatic, "isStatic", "Is Static",      "Transform"),

            // ★ ENTITY_ID → ENTITY_LINK に変更。
            //   - 保存: parent.id を SaveContext でローカルインデックスに変換して書き出す
            //   - 復元: ローカルインデックスを LoadContext で新 EntityID に解決して書き込む
            //   - 旧コード (if components.contains("Transform") ...) は不要になる
            META_FIELD_ENTITY_LINK(TransformComponent, parent, "parent", "Parent", "Hierarchy"),
        };
        return fields;
    }
};

// ============================================================================
// 登録
//   REGISTER_COMPONENT      : ランタイム / エディタ 共通 (ComponentRegistry)
//   REGISTER_COMPONENT_EDITOR_GUI : WITH_EDITOR のみ有効 (ImGui インスペクタ)
// ============================================================================
REGISTER_COMPONENT(TransformComponent, "Transform")
REGISTER_COMPONENT_EDITOR_GUI(TransformComponent)