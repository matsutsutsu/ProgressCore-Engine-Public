#include "Editor/Inspector/ComponentGuiRegistry.h"
#include "Engine/GamePlay/Transform/TransformComponent.h"
#include "Engine/Serialization/ComponentRegistry.h"
#include "Engine/Serialization/Meta/ComponentMeta.h"
#include "Engine/Serialization/Meta/ComponentMetaImGui.h"
#include "Engine/Serialization/Meta/ComponentMetaJson.h"

#include "Engine/Serialization/ComponentSerializers.h"

template <> struct ComponentMeta<TransformComponent> {
    static constexpr bool        registered  = true;
    static constexpr const char *displayName = "Transform";

    //  直感的な編集のためカスタムGUIを有効化
    static constexpr bool hasCustomGui = true;

    static bool CustomGui(TransformComponent &comp)
    {
        bool changed = false;

        // 1. 位置
        changed |= ImGui::DragFloat3("Position", &comp.position.x, 0.1f);

        // 2. 回転 (Quaternion -> XYZ Euler変換のUIを将来的に入れるのがAAAの定石です)
        // 今回は直接四元数を触れるようにしていますが、後日Euler変換UIの実装を推奨します。
        changed |= ImGui::DragFloat4("Rotation (Quat)", &comp.rotation.x, 0.05f);

        // 3. スケール
        changed |= ImGui::DragFloat3("Scale", &comp.scale.x, 0.1f);

        ImGui::Spacing();
        changed |= ImGui::Checkbox("Is Static (静的オブジェクト)", &comp.isStatic);

        return changed;
    }

    //  JSON自動シリアライズ用の定義（親兄弟のIDなどは保存しない）
    static const std::vector<FieldDescriptor> &Fields()
    {
        static const std::vector<FieldDescriptor> fields = {
            META_FIELD_FLOAT3(
                TransformComponent, position, "position", "Position", 0.1f, "Transform"),
            META_FIELD_FLOAT4(TransformComponent,
                rotation,
                "rotation",
                "Rotation",
                0.1f,
                "Transform"), // ComponentMeta.h に META_FIELD_FLOAT4 が必要です
            META_FIELD_FLOAT3(TransformComponent, scale, "scale", "Scale", 0.1f, "Transform"),
            META_FIELD_BOOL(TransformComponent, isStatic, "isStatic", "Is Static", "Transform")};
        return fields;
    }
};


#include <windows.h> // OutputDebugString用
#include <string>

void RegisterTransformMeta()
{
    auto &jsonReg = ComponentRegistry::Instance();
    ComponentMetaJson::RegisterMetaT<TransformComponent>(jsonReg, "Transform");

    auto &guiReg = ComponentGuiRegistry::Instance();
    ComponentMetaImGui::RegisterGuiMeta<TransformComponent>(guiReg);

    // ★ デバッグ出力：登録側の真実
    std::string log =
        "[Register] Transform ID: " + std::to_string(CCL::ECS::TypeInfo<TransformComponent>::ID()) +
        " | GuiReg Addr: " + std::to_string(reinterpret_cast<uintptr_t>(&guiReg)) + "\n";
    OutputDebugStringA(log.c_str());

}