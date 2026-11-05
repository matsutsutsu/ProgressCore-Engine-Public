#include "Engine/Cinematic/Components/CinematicCameraComponent.h"
#include "Engine/Serialization/Meta/ComponentDefine.h"
#include "Engine/Serialization/Meta/ComponentCategories.h"

#ifdef ENABLE_EDITOR
#include "Editor/Inspector/ComponentGuiRegistry.h"
#include "Engine/Serialization/Meta/ComponentMetaImGui.h"
#endif

COMPONENT_META_BEGIN_FULL(CinematicCameraComponent, "CinematicCamera", CCL::Categories::Camera, 370, false)
CFIELD_INT_RANGED(activePriority, "再生中の優先度", -1000, 1000, "Priority")
CFIELD_INT_RANGED(idlePriority, "停止中の優先度", -1000, 1000, "Priority")
CFIELD_ENUM_U8_RT(state, "状態", kCinematicStateNames, kCinematicStateCount, "State")
CFIELD_RT(time, "再生位置（秒）", "State")
CFIELD_RT(waitTime, "開始待ちの経過（秒）", "State")
CFIELD_RT(activeShot, "今のショット番号", "State")
COMPONENT_META_END(CinematicCameraComponent)
