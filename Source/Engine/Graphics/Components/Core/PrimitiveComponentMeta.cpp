/// @file  PrimitiveComponentMeta.cpp
/// @brief プリミティブ描画用コンポーネントのシリアライズおよびインスペクタ定義。

#include "PrimitiveComponent.h"

// ★ マニュアル絶対ルール：必須ヘッダー
#include "Engine/Serialization/Meta/ComponentDefine.h"
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
    static constexpr bool        hasCustomGui   = true;  // ★ 動的UX実現のためカスタムGUIを有効化
    static constexpr bool        isSerializable = true;

#ifdef ENABLE_EDITOR
    static bool CustomGui(PrimitiveComponent& comp, unsigned long long /*entityID*/ = 0, void* /*world*/ = nullptr) {
        bool changed = false;

        ImGui::SeparatorText("Shape Settings");

        // 1. 形状の選択 (プルダウン)
        int currentType = static_cast<int>(comp.type);
        if (ImGui::Combo("Shape Type", &currentType, PrimitiveTypeNames, std::size(PrimitiveTypeNames))) {
            comp.type = static_cast<PrimitiveType>(currentType);
            changed = true;
        }

        ImGui::SeparatorText("Dimensions");
        
        // 2. 選択された形状に応じて、不要なパラメータ項目を隠す（UX最適化）
        if (comp.type == PrimitiveType::Box) {
            changed |= ImGui::DragFloat3("Size (Box)", &comp.size.x, 0.1f);
        } 
        else if (comp.type == PrimitiveType::Sphere) {
            changed |= ImGui::DragFloat("Radius", &comp.radius, 0.1f, 0.0f, 1000.0f);
        } 
        else {
            // Cylinder, Capsule, Cone
            changed |= ImGui::DragFloat("Radius", &comp.radius, 0.1f, 0.0f, 1000.0f);
            changed |= ImGui::DragFloat("Height", &comp.height, 0.1f, 0.0f, 1000.0f);
        }

        ImGui::SeparatorText("Appearance");
        
        // 3. 直感的なカラーピッカーを使用
        changed |= ImGui::ColorEdit4("Color", &comp.color.x);
        
        changed |= ImGui::Checkbox("Wireframe", &comp.isWireframe);

        return changed;
    }
#else
    // フォールバック
    static bool CustomGui(PrimitiveComponent&, unsigned long long, void*) { return false; }
#endif

    // ============================================================================
    // 自動シリアライズ（JSON保存/ロード）用の定義
    // ============================================================================
    static const std::vector<FieldDescriptor>& Fields() {
        // ★ CFIELDマクロ群（コンパイラエラーを防ぐため、末尾のカンマは絶対につけない）
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