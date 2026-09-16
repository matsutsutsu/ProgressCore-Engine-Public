// カリング関連コンポーネント (BoundingBox, Visibility) のメタ定義。
// ランタイム専用データとして扱うため、isSerializable = false に設定し、
// インスペクタ表示のみを自動有効化する。

#include "CullingComponents.h"
#include "Engine/Graphics/Components/Core/NoRenderTag.h" // 当たり判定専用ジオメトリ用タグ
#include "Engine/Graphics/Components/Core/NoShadowTag.h" // 影を落とさせない用タグ
#include "Engine/Serialization/Meta/ComponentDefine.h"
#include "Engine/Serialization/Meta/ComponentCategories.h"

#ifdef ENABLE_EDITOR
#include "Editor/Inspector/ComponentGuiRegistry.h"
#include "Engine/Serialization/Meta/ComponentMetaImGui.h"
#endif

// ===========================================================================
// BoundingBoxComponent
// ===========================================================================
template <>
struct ComponentMeta<BoundingBoxComponent>
{
    CCL_META_USING_THIS(BoundingBoxComponent)

    static constexpr const char *displayName       = "Bounding Box";
    static constexpr const char *category          = CCL::Categories::Internal; // 内部ステートとして隔離
    static constexpr int         sortPriority      = 900;                       // インスペクタの最下層へ
    static constexpr bool        hiddenInInspector = false;
    static constexpr bool        hasCustomGui      = false;
    static constexpr bool        isSerializable    = false;

#ifdef ENABLE_EDITOR
    static bool CustomGui(BoundingBoxComponent &, unsigned long long, void *) { return false; }
#else
    static bool CustomGui(BoundingBoxComponent &, unsigned long long, void *) { return false; }
#endif

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

#ifdef ENABLE_EDITOR
    static bool CustomGui(VisibilityComponent &, unsigned long long, void *) { return false; }
#else
    static bool CustomGui(VisibilityComponent &, unsigned long long, void *) { return false; }
#endif

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
//   「描画しない（当たり判定専用）」を表すタグ。付いていること自体が意味を持つ
//   ためフィールドは持たない。見えない壁/床に使う。
//   作者が付ける設定データなので isSerializable = true（シーン/プレハブに保存）。
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
    static constexpr bool        isSerializable    = true; // 保存する（次回ロードでも透明のまま）

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
//   「影を落とさない」を表すタグ。通常描画には出る点が NoRenderTag と違う。
//   カスケード3枚ぶんのラスタライズを丸ごと省けるので、大量に出る小物
//   （ドローン等）に付けると描画負荷が大きく下がる。
//   絵の判断なので作者が付ける設定データ。isSerializable = true。
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
    static constexpr bool        isSerializable    = true; // 保存する（次回ロードでも影なしのまま）

    static bool CustomGui(NoShadowTag &, unsigned long long, void *) { return false; }

    static const std::vector<FieldDescriptor> &Fields()
    {
        // フィールド無し（タグの有無だけが意味を持つ）
        static const std::vector<FieldDescriptor> fields = {};
        return fields;
    }
};
// 登録名は displayName と一致させる（プレハブ JSON のキーになるため、
// 表示名とズレると「付けたのに読まれない」無言の不具合になる）。
REGISTER_COMPONENT(NoShadowTag, ComponentMeta<NoShadowTag>::displayName)
REGISTER_COMPONENT_EDITOR_GUI(NoShadowTag)