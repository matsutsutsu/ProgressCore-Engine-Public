/// @file  KnockbackComponentMeta.cpp
/// @brief KnockbackComponent のシリアライズ/インスペクタ定義。
///        ランタイム値(velocity/timer)は Fields() に載せない＝保存対象外。エディタで
///        エンティティに追加できるようにするための登録もここで行う。

#include "KnockbackComponent.h"
#include "Engine/Serialization/Meta/ComponentDefine.h"
#include "Engine/Serialization/Meta/ComponentCategories.h"

// ===========================================================================
// KnockbackComponent メタデータ定義（Movement カテゴリ）
// ===========================================================================
COMPONENT_META_BEGIN_FULL(KnockbackComponent, "Knockback", CCL::Categories::Movement, 155, false)
    CFIELD_FLOAT_RANGED(duration, "Duration (s)", 0.005f, 0.0f, 2.0f, "Knockback")
COMPONENT_META_END(KnockbackComponent)
