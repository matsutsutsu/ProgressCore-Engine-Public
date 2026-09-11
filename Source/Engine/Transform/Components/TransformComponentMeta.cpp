// TransformComponent のシリアライズ/インスペクタ定義。

#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/Serialization/Meta/ComponentDefine.h"
#include "Engine/Serialization/Meta/ComponentCategories.h"
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
    CCL_META_USING_THIS(TransformComponent)   

    static constexpr const char* displayName    = "Transform";
    static constexpr const char* category       = CCL::Categories::Transform;
    static constexpr int         sortPriority   = 0; // マニュアル通り「常に最上段」を強制
    static constexpr bool        hiddenInInspector = false;
    static constexpr bool        hasCustomGui   = true;
    static constexpr bool        isSerializable = true;

#ifdef ENABLE_EDITOR
    static bool CustomGui(TransformComponent& comp, unsigned long long /*entityID*/ = 0, void* /*worldPtr*/ = nullptr) {
        namespace mi = ComponentMetaImGui;
        bool changed = false;

        if (mi::DragFloat3("位置", &comp.position.x, 0.1f)) {
            comp.isTeleported = true; comp.isStatic = false; changed = true;
        }

        DirectX::XMFLOAT3 euler = QuatToEulerDegrees(comp.rotation);
        if (mi::DragFloat3("回転", &euler.x, 1.0f)) {
            comp.rotation = EulerDegreesToQuat(euler);
            comp.isTeleported = true; comp.isStatic = false; changed = true;
        }

        if (mi::DragFloat3("スケール", &comp.scale.x, 0.1f)) {
            comp.isStatic = false; changed = true;
        }

        ImGui::Spacing();
        changed |= mi::Checkbox("静的(Static)", &comp.isStatic);
        return changed;
    }
#else
    static bool CustomGui(TransformComponent&, unsigned long long, void*) { return false; }
#endif

    static const std::vector<FieldDescriptor>& Fields() {
        static const std::vector<FieldDescriptor> fields = {
            CFIELD(position, "位置", "Transform")
            CFIELD(rotation, "回転", "Transform")
            CFIELD(scale,    "スケール",    "Transform")
            CFIELD(isStatic, "静的(Static)","Transform")
            CFIELD(parent,   "親",   "Hierarchy")
        };
        return fields;
    }
};

REGISTER_COMPONENT(TransformComponent, "Transform")
REGISTER_COMPONENT_EDITOR_GUI(TransformComponent)