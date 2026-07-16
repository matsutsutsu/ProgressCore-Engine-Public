/// @file  PrimitiveComponentMeta.cpp
/// @brief プリミティブ描画用コンポーネントのシリアライズおよびインスペクタ定義。

#include "PrimitiveComponent.h"
#include "Engine/Serialization/Meta/ComponentDefine.h"
#include "Engine/Serialization/Meta/ComponentCategories.h" // ★追加
#include <iterator>

#ifdef ENABLE_EDITOR
#include <imgui.h>
#include "Editor/Inspector/ComponentGuiRegistry.h"
#include "Engine/Serialization/Meta/ComponentMetaImGui.h"
#endif

// ============================================================================
// エディタのプルダウン表示用の文字列定義
// ============================================================================
static const char* PrimitiveTypeNames[] = {
    "Box", "Sphere", "Cylinder", "Capsule", "Cone"
};

template <> struct ComponentMeta<PrimitiveComponent> {
    CCL_META_USING_THIS(PrimitiveComponent)

    static constexpr const char* displayName    = "Primitive Renderer";
    static constexpr const char* category       = CCL::Categories::Rendering; // ★修正
    static constexpr int         sortPriority   = 80; // ★追加: 描画系の並び順
    static constexpr bool        hiddenInInspector = false;
    static constexpr bool        hasCustomGui   = true;
    static constexpr bool        isSerializable = true;

#ifdef ENABLE_EDITOR
    static bool CustomGui(PrimitiveComponent& comp, unsigned long long /*entityID*/ = 0, void* /*world*/ = nullptr) {
        bool changed = false;

        ImGui::SeparatorText("Shape Settings");

        int currentType = static_cast<int>(comp.type);
        if (ImGui::Combo("Shape Type", &currentType, PrimitiveTypeNames, std::size(PrimitiveTypeNames))) {
            comp.type = static_cast<PrimitiveType>(currentType);
            changed = true;
        }

        ImGui::SeparatorText("Dimensions");
        
        if (comp.type == PrimitiveType::Box) {
            changed |= ImGui::DragFloat3("Size (Box)", &comp.size.x, 0.1f);
        } 
        else if (comp.type == PrimitiveType::Sphere) {
            changed |= ImGui::DragFloat("Radius", &comp.radius, 0.1f, 0.0f, 1000.0f);
        } 
        else {
            changed |= ImGui::DragFloat("Radius", &comp.radius, 0.1f, 0.0f, 1000.0f);
            changed |= ImGui::DragFloat("Height", &comp.height, 0.1f, 0.0f, 1000.0f);
        }

        ImGui::SeparatorText("Appearance");
        
        changed |= ImGui::ColorEdit4("Color", &comp.color.x);
        changed |= ImGui::Checkbox("Wireframe", &comp.isWireframe);

        return changed;
    }
#else
    static bool CustomGui(PrimitiveComponent&, unsigned long long, void*) { return false; }
#endif

    static const std::vector<FieldDescriptor>& Fields() {
        static const std::vector<FieldDescriptor> fields = {
            CFIELD_ENUM_INT(type, "Shape Type", PrimitiveTypeNames, std::size(PrimitiveTypeNames), "Appearance")
            CFIELD_FLOAT3_FAST(size, "Size", 0.1f, "Dimensions")
            CFIELD_FLOAT_RANGED(radius, "Radius", 0.1f, 0.0f, 1000.0f, "Dimensions")
            CFIELD_FLOAT_RANGED(height, "Height", 0.1f, 0.0f, 1000.0f, "Dimensions")
            CFIELD(color, "Color", "Appearance")
            CFIELD(isWireframe, "Wireframe", "Appearance")
        };
        return fields;
    }
};

REGISTER_COMPONENT(PrimitiveComponent, "Primitive")
REGISTER_COMPONENT_EDITOR_GUI(PrimitiveComponent)