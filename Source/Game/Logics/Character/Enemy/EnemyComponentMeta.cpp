/// @file  EnemyComponentMeta.cpp
/// @brief EnemyComponent のシリアライズ/インスペクタ定義。
///        移動速度・攻撃射程・攻撃力を編集可能にする。

#include "EnemyComponent.h"
#include "Engine/Serialization/Meta/ComponentDefine.h"

COMPONENT_META_BEGIN(EnemyComponent, "Enemy")
CFIELD_FLOAT_RANGED(moveSpeed, "Move Speed", 0.1f, 0.0f, 100.0f, "AI")
CFIELD_FLOAT_RANGED(attackRange, "Attack Range", 0.1f, 0.0f, 50.0f, "AI")
CFIELD_FLOAT_RANGED(attackDamage, "Attack Damage", 0.5f, 0.0f, 999.0f, "AI")
COMPONENT_META_END(EnemyComponent)