// ジャスト回避演出(JustEvadeEffectComponent)のシリアライズ/インスペクタ定義。
// ランタイム状態(timer)は Fields() に載せない＝保存対象外。

#include "Engine/ECS/Core/CCL_World.h"
#include "Game/Logics/Effects/Components/JustEvadeEffectComponent.h"

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
// JustEvadeEffectComponent（Effect カテゴリ。シーンに singleton で1つ置く）
//   style は3種の色味を切り替えるドロップダウン（見比べ確認用）。
// ===========================================================================
COMPONENT_META_BEGIN_FULL(JustEvadeEffectComponent, "Just Evade Effect", CCL::Categories::Effect, 186, false)
    CFIELD_ENUM_U8(style,        "カラースタイル",      JustEvadeStyleNames, JustEvadeStyleCount, "スタイル")
    CFIELD_FLOAT_RANGED(duration,     "継続時間(秒)",       0.005f, 0.0f, 2.0f,  "エンベロープ")
    CFIELD_FLOAT_RANGED(attackTime,   "立ち上がり(秒)",         0.005f, 0.0f, 0.5f,  "エンベロープ")
    CFIELD_FLOAT_RANGED(releaseTime,  "減衰(秒)",        0.005f, 0.0f, 1.0f,  "エンベロープ")
    CFIELD_FLOAT_RANGED(spikeDecay,   "スパイク減衰(秒)",    0.005f, 0.0f, 1.0f,  "エンベロープ")
    CFIELD_FLOAT_RANGED(radialPeak,   "放射ブラー強度",   0.01f,  0.0f, 2.0f,  "ポストプロセス")
    CFIELD_FLOAT_RANGED(chromaPeak,   "色収差強度",     0.001f, 0.0f, 0.2f,  "ポストプロセス")
    CFIELD_FLOAT_RANGED(gradeMixPeak, "カラーグレード量", 0.01f,  0.0f, 1.0f,  "ポストプロセス")
    CFIELD_FLOAT_RANGED(vignettePeak, "ビネット強度",      0.01f,  0.0f, 1.0f,  "ポストプロセス")
    CFIELD_FLOAT_RANGED(vignettePulse,"ビネット脈動",     0.1f,   0.0f, 20.0f, "ポストプロセス")
COMPONENT_META_END(JustEvadeEffectComponent)
