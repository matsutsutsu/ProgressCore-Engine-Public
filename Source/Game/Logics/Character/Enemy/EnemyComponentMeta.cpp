/// @file  EnemyComponentMeta.cpp
/// @brief EnemyComponent のシリアライズ/インスペクタ定義。

#include "EnemyComponent.h"
#include "Engine/Serialization/Meta/ComponentDefine.h"
#include "Engine/Serialization/Meta/ComponentCategories.h" // ★追加

// ★修正: BEGIN_FULL を使用して Gameplay カテゴリに分類
COMPONENT_META_BEGIN_FULL(EnemyComponent, "Enemy", CCL::Categories::Gameplay, 140, false)
CFIELD_FLOAT_RANGED(moveSpeed, "Move Speed", 0.1f, 0.0f, 100.0f, "AI")
CFIELD_FLOAT_RANGED(attackRange, "Attack Range", 0.1f, 0.0f, 50.0f, "AI")
CFIELD_FLOAT_RANGED(attackDamage, "Attack Damage", 0.5f, 0.0f, 999.0f, "AI")
COMPONENT_META_END(EnemyComponent)