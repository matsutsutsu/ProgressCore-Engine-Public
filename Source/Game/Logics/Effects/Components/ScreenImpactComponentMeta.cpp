// 命中演出(ScreenImpactComponent)のシリアライズ/インスペクタ定義。
// ランタイム状態(timer/power/seed)は Fields() に載せない＝保存対象外。

#include "Engine/ECS/Core/CCL_World.h"
#include "Game/Logics/Effects/Components/ScreenImpactComponent.h"

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
// ScreenImpactComponent（Effect カテゴリ。シーンに singleton で1つ置く）
// ===========================================================================
COMPONENT_META_BEGIN_FULL(ScreenImpactComponent, "Screen Impact", CCL::Categories::Effect, 185, false)
CFIELD_FLOAT_RANGED(duration, "継続時間(秒)", 0.005f, 0.0f, 1.0f, "衝撃")
CFIELD_FLOAT_RANGED(shakeTrauma, "1発の衝撃量", 0.005f, 0.0f, 1.0f, "カメラ揺れ")
CFIELD_FLOAT_RANGED(radialStrength, "放射ブラー強度", 0.01f, 0.0f, 2.0f, "ポストプロセス")
CFIELD_FLOAT_RANGED(chromaticStrength, "色収差強度", 0.001f, 0.0f, 0.2f, "ポストプロセス")
COMPONENT_META_END(ScreenImpactComponent)
