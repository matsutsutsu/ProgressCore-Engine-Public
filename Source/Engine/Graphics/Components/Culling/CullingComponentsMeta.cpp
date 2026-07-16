/// @file  CullingComponentMeta.cpp
/// @brief カリング関連コンポーネント (BoundingBox, Visibility) のメタ定義。
///        ランタイム専用データとして扱うため、isSerializable = false に設定し、
///        インスペクタ表示のみを自動有効化する。

#include "CullingComponents.h"
#include "Engine/Serialization/Meta/ComponentDefine.h"
#include "Engine/Serialization/Meta/ComponentCategories.h" // ★追加

#ifdef ENABLE_EDITOR
#include "Editor/Inspector/ComponentGuiRegistry.h"
#include "Engine/Serialization/Meta/ComponentMetaImGui.h"
#endif

// ===========================================================================
// BoundingBoxComponent 
// ===========================================================================
template <> struct ComponentMeta<BoundingBoxComponent> {
    CCL_META_USING_THIS(BoundingBoxComponent)

    static constexpr const char* displayName    = "Bounding Box";
    static constexpr const char* category       = CCL::Categories::Internal; // ★修正: 内部ステートとして隔離
    static constexpr int         sortPriority   = 900; // ★追加: インスペクタの最下層へ
    static constexpr bool        hiddenInInspector = false;
    static constexpr bool        hasCustomGui   = false; 
    static constexpr bool        isSerializable = false; 

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
// VisibilityComponent 
// ===========================================================================
template <> struct ComponentMeta<VisibilityComponent> {
    CCL_META_USING_THIS(VisibilityComponent)

    static constexpr const char* displayName    = "Visibility";
    static constexpr const char* category       = CCL::Categories::Internal; // ★修正
    static constexpr int         sortPriority   = 910; // ★追加
    static constexpr bool        hiddenInInspector = false;
    static constexpr bool        hasCustomGui   = false; 
    static constexpr bool        isSerializable = false; 

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