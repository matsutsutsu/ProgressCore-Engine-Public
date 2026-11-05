#include "PlayerJustEvadeComponent.h"
#include "Engine/Serialization/Meta/ComponentDefine.h"
#include "Engine/Serialization/Meta/ComponentCategories.h"

COMPONENT_META_BEGIN_FULL(PlayerJustEvadeComponent, "Player Just Evade Component", CCL::Categories::Movement, 156, false)
CFIELD(isJustSuccess, "ジャスト成功", "状態")
COMPONENT_META_END(PlayerJustEvadeComponent)
