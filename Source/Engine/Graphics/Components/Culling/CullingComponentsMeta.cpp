/// @file  CullingComponentMeta.cpp
/// @brief カリング関連コンポーネント (BoundingBox, Visibility) のメタ定義。
///        ランタイム専用データとして扱うため、isSerializable = false に設定し、
///        インスペクタ表示のみを自動有効化する。

#include "CullingComponents.h"
#include "Engine/Serialization/Meta/ComponentDefine.h"

#ifdef ENABLE_EDITOR
#include "Editor/Inspector/ComponentGuiRegistry.h"
#include "Engine/Serialization/Meta/ComponentMetaImGui.h"
#endif

// ===========================================================================
// BoundingBoxComponent (インスペクタ描画のみ・シリアライズ対象外)
// ===========================================================================
template <> struct ComponentMeta<BoundingBoxComponent> {
    CCL_META_USING_THIS(BoundingBoxComponent)
        static constexpr const char* displayName = "Bounding Box";
    static constexpr bool        hasCustomGui = false; // 自動インスペクタを利用
    static constexpr bool        isSerializable = false; // ★ JSONへの保存をスキップ

#ifdef ENABLE_EDITOR
    static bool CustomGui(BoundingBoxComponent&, unsigned long long, void*) { return false; }
#else
    static bool CustomGui(BoundingBoxComponent&, unsigned long long, void*) { return false; }
#endif

    static const std::vector<FieldDescriptor>& Fields() {
        static const std::vector<FieldDescriptor> fields = {
            CFIELD_FLOAT3_FAST(worldAABB.Center, "Center", 0.1f, "Culling")
            CFIELD_FLOAT3_FAST(worldAABB.Extents, "Extents", 0.1f, "Culling")
        };
        return fields;
    }
};
REGISTER_COMPONENT(BoundingBoxComponent, "Bounding Box")
REGISTER_COMPONENT_EDITOR_GUI(BoundingBoxComponent)

// ===========================================================================
// VisibilityComponent (インスペクタ描画のみ・シリアライズ対象外)
// ===========================================================================
template <> struct ComponentMeta<VisibilityComponent> {
    CCL_META_USING_THIS(VisibilityComponent)
        static constexpr const char* displayName = "Visibility";
    static constexpr bool        hasCustomGui = false; // 自動インスペクタを利用
    static constexpr bool        isSerializable = false; // ★ JSONへの保存をスキップ

#ifdef ENABLE_EDITOR
    static bool CustomGui(VisibilityComponent&, unsigned long long, void*) { return false; }
#else
    static bool CustomGui(VisibilityComponent&, unsigned long long, void*) { return false; }
#endif

    static const std::vector<FieldDescriptor>& Fields() {
        static const std::vector<FieldDescriptor> fields = {
            CFIELD(isVisible, "Is Visible", "Culling")
        };
        return fields;
    }
};
REGISTER_COMPONENT(VisibilityComponent, "Visibility")
REGISTER_COMPONENT_EDITOR_GUI(VisibilityComponent)