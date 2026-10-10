// JustEvadeEffectComponent のシリアライズ/インスペクタ定義。

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

// 実行時の状態（active / weight）は保存しないので並べない
COMPONENT_META_BEGIN_FULL(JustEvadeEffectComponent, "Just Evade Effect", CCL::Categories::Effect, 186, false)
CFIELD_FLOAT_RANGED(vignetteIntensity, "ビネット強度", 0.01f, 0.0f, 1.0f, "ビネット")
CFIELD_FLOAT_RANGED(vignetteRadius, "ビネット半径", 0.01f, 0.0f, 1.0f, "ビネット")
CFIELD_FLOAT_RANGED(fadeInTime, "フェードイン(秒)", 0.005f, 0.0f, 1.0f, "エンベロープ")
CFIELD_FLOAT_RANGED(fadeOutTime, "フェードアウト(秒)", 0.005f, 0.0f, 2.0f, "エンベロープ")
COMPONENT_META_END(JustEvadeEffectComponent)
