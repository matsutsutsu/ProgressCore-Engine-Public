// BoundingBox・Visibility・NoRenderTag・NoShadowTag のシリアライズ/インスペクタ定義。

#include "CullingComponents.h"
#include "Engine/Graphics/Components/Core/NoRenderTag.h"
#include "Engine/Graphics/Components/Core/NoShadowTag.h"
#include "Engine/Serialization/Meta/ComponentDefine.h"
#include "Engine/Serialization/Meta/ComponentCategories.h"

#ifdef ENABLE_EDITOR
#include "Editor/Inspector/ComponentGuiRegistry.h"
#include "Engine/Serialization/Meta/ComponentMetaImGui.h"
#endif

// ===========================================================================
// BoundingBoxComponent
//   実行時に計算する値なので保存しない（isSerializable = false）。確認用に表示だけする。
// ===========================================================================
template <>
struct ComponentMeta<BoundingBoxComponent>
{
    CCL_META_USING_THIS(BoundingBoxComponent)

    static constexpr const char *displayName       = "Bounding Box";
    static constexpr const char *category          = CCL::Categories::Internal;
    static constexpr int         sortPriority      = 900; // インスペクタの一番下に並べる
    static constexpr bool        hiddenInInspector = false;
    static constexpr bool        hasCustomGui      = false;
    static constexpr bool        isSerializable    = false;

    static bool CustomGui(BoundingBoxComponent &, unsigned long long, void *) { return false; }

    static const std::vector<FieldDescriptor> &Fields()
    {
        static const std::vector<FieldDescriptor> fields = {
            CFIELD_FLOAT3_FAST(worldAABB.Center, "中心", 0.1f, "カリング")
            CFIELD_FLOAT3_FAST(worldAABB.Extents, "範囲(半径)", 0.1f, "カリング")};
        return fields;
    }
};
REGISTER_COMPONENT(BoundingBoxComponent, "Bounding Box")
REGISTER_COMPONENT_EDITOR_GUI(BoundingBoxComponent)

// ===========================================================================
// VisibilityComponent
//   FrustumCullingSystem が毎フレーム書き換えるので保存しない。
// ===========================================================================
template <>
struct ComponentMeta<VisibilityComponent>
{
    CCL_META_USING_THIS(VisibilityComponent)

    static constexpr const char *displayName       = "Visibility";
    static constexpr const char *category          = CCL::Categories::Internal;
    static constexpr int         sortPriority      = 910;
    static constexpr bool        hiddenInInspector = false;
    static constexpr bool        hasCustomGui      = false;
    static constexpr bool        isSerializable    = false;

    static bool CustomGui(VisibilityComponent &, unsigned long long, void *) { return false; }

    static const std::vector<FieldDescriptor> &Fields()
    {
        static const std::vector<FieldDescriptor> fields = {
            CFIELD(isVisible, "表示中", "カリング")};
        return fields;
    }
};
REGISTER_COMPONENT(VisibilityComponent, "Visibility")
REGISTER_COMPONENT_EDITOR_GUI(VisibilityComponent)

// ===========================================================================
// NoRenderTag
//   付いているかどうかだけが意味を持つので、フィールドは無い。
//   シーンを作る人が付ける設定なので、シーン/プレハブに保存する。
// ===========================================================================
template <>
struct ComponentMeta<NoRenderTag>
{
    CCL_META_USING_THIS(NoRenderTag)

    static constexpr const char *displayName       = "No Render (Collider Only)";
    static constexpr const char *category          = CCL::Categories::Rendering;
    static constexpr int         sortPriority      = 920;
    static constexpr bool        hiddenInInspector = false;
    static constexpr bool        hasCustomGui      = false;
    static constexpr bool        isSerializable    = true;

    static bool CustomGui(NoRenderTag &, unsigned long long, void *) { return false; }

    static const std::vector<FieldDescriptor> &Fields()
    {
        // フィールド無し（タグの有無だけが意味を持つ）
        static const std::vector<FieldDescriptor> fields = {};
        return fields;
    }
};
REGISTER_COMPONENT(NoRenderTag, "NoRenderTag")
REGISTER_COMPONENT_EDITOR_GUI(NoRenderTag)

// ===========================================================================
// NoShadowTag
//   NoRenderTag と同じく、フィールドを持たずシーン/プレハブに保存する。
// ===========================================================================
template <>
struct ComponentMeta<NoShadowTag>
{
    CCL_META_USING_THIS(NoShadowTag)

    static constexpr const char *displayName       = "No Shadow";
    static constexpr const char *category          = CCL::Categories::Rendering;
    static constexpr int         sortPriority      = 921;
    static constexpr bool        hiddenInInspector = false;
    static constexpr bool        hasCustomGui      = false;
    static constexpr bool        isSerializable    = true;

    static bool CustomGui(NoShadowTag &, unsigned long long, void *) { return false; }

    static const std::vector<FieldDescriptor> &Fields()
    {
        // フィールド無し（タグの有無だけが意味を持つ）
        static const std::vector<FieldDescriptor> fields = {};
        return fields;
    }
};
// 登録名は displayName から取る。登録名はプレハブ JSON のキーになるので、
// 表示名とずれると、表示名どおりにキーを書いても読み込まれない（エラーも出ない）。
REGISTER_COMPONENT(NoShadowTag, ComponentMeta<NoShadowTag>::displayName)
REGISTER_COMPONENT_EDITOR_GUI(NoShadowTag)