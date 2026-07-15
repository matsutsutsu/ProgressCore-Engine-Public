/// @file  TrailComponentMeta.cpp
/// @brief 武器トレイル(TrailComponent)のシリアライズ/インスペクタ定義。
///        ランタイムのリングバッファ(segments/head/count)は Fields() に載せない
///        ＝保存対象外。config フィールドのみ保存・編集する。

#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/Graphics/Components/Trail/TrailComponent.h"

#include "Engine/Serialization/Meta/ComponentDefine.h"
#include "Engine/Serialization/Meta/ComponentMetaJson.h"
#include "Engine/Serialization/Meta/ComponentCategories.h"

#ifdef ENABLE_EDITOR
#include "Editor/Inspector/ComponentGuiRegistry.h"
#include "Engine/Serialization/Meta/ComponentMetaImGui.h"
#endif

#include <json.hpp>
using json = nlohmann::json;

// ===========================================================================
// TrailComponent（Effect カテゴリ。Model/Material の少し後ろに並べる）
// ===========================================================================
COMPONENT_META_BEGIN_FULL(TrailComponent, "Weapon Trail", CCL::Categories::Effect, 180, false)
    CFIELD_FLOAT3_FAST(baseOffset, "Base Offset (根本)", 0.01f, "Shape")
    CFIELD_FLOAT3_FAST(tipOffset,  "Tip Offset (先端)",  0.01f, "Shape")
    CFIELD_COLOR(color, "Color (加算)", "Look")
    CFIELD_FLOAT_RANGED(lifeTime,    "Life Time (s)", 0.005f, 0.0f, 2.0f, "Look")
    CFIELD_FLOAT_RANGED(minDistance, "Min Distance",  0.005f, 0.0f, 1.0f, "Shape")
    CFIELD(emitting, "Emitting", "State")
    CFIELD_T(animControlled, "Anim Controlled", "State",
             "ONで Trail アニメイベントの区間中だけ発生。OFFは手動(Emitting)運用。")
COMPONENT_META_END(TrailComponent)
