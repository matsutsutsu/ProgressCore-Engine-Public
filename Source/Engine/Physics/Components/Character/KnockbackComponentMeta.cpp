#include "KnockbackComponent.h"
#include "Engine/Serialization/Meta/ComponentDefine.h"
#include "Engine/Serialization/Meta/ComponentCategories.h"

COMPONENT_META_BEGIN_FULL(KnockbackComponent, "Knockback", CCL::Categories::Movement, 155, false)
CFIELD_FLOAT_RANGED(duration, "継続時間(秒)", 0.005f, 0.0f, 2.0f, "ノックバック")
CFIELD_FLOAT_RANGED(stunDuration, "硬直時間(秒)", 0.005f, 0.0f, 2.0f, "ノックバック")
CFIELD_RO(stunTimer, "硬直の残り(秒)", "ノックバック", "0より大きい間はプレイヤー操作を受け付けない。")
COMPONENT_META_END(KnockbackComponent)
