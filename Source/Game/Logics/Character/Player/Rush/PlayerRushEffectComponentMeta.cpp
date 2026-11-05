#include "Engine/ECS/Core/CCL_World.h"
#include "Game/Logics/Character/Player/Rush/PlayerRushEffectComponent.h"

#include "Engine/Serialization/Meta/ComponentDefine.h"
#include "Engine/Serialization/Meta/ComponentMetaJson.h"
#include "Engine/Serialization/Meta/ComponentCategories.h"

#ifdef ENABLE_EDITOR
#include "Editor/Inspector/ComponentGuiRegistry.h"
#include "Engine/Serialization/Meta/ComponentMetaImGui.h"
#endif

#include <json.hpp>
using json = nlohmann::json;

// 実行時の状態（rushActive / weight）は保存しないので並べない
COMPONENT_META_BEGIN_FULL(PlayerRushEffectComponent, "Player Rush Effect", CCL::Categories::Effect, 186, false)
CFIELD_FLOAT_RANGED(vignetteIntensity, "ビネット強度", 0.01f, 0.0f, 1.0f, "ビネット")
CFIELD_FLOAT_RANGED(vignetteRadius, "ビネット半径", 0.01f, 0.0f, 1.0f, "ビネット")
CFIELD_FLOAT_RANGED(radialStrength, "放射ブラー強度", 0.005f, 0.0f, 1.0f, "放射ブラー")
CFIELD_FLOAT_RANGED(radialFalloff, "放射ブラー減衰", 0.05f, 0.0f, 8.0f, "放射ブラー")
CFIELD_FLOAT_RANGED(fadeInTime, "フェードイン(秒)", 0.005f, 0.0f, 1.0f, "エンベロープ")
CFIELD_FLOAT_RANGED(fadeOutTime, "フェードアウト(秒)", 0.005f, 0.0f, 2.0f, "エンベロープ")
COMPONENT_META_END(PlayerRushEffectComponent)
