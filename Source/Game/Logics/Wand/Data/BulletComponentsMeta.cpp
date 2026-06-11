// ===========================================================================
// Data/BulletComponents.cpp
// -------------------------
// @file
// @brief 弾コンポーネントのMeta（実行時の値をインスペクタで監視するため）。
// @note 弾は実行時生成のためシーンには配置しない。値の確認用。
//       direction(Vector3)/lastHitTarget(EntityID) は型対応が環境依存のため省略。
//       FieldDescriptor が対応していれば CFIELD を足してよい。
// ===========================================================================
#include "Game/Logics/Wand/Data/BulletComponents.h"
#include "Engine/Serialization/Meta/ComponentDefine.h"

COMPONENT_META_BEGIN(BulletProgramRef, "Bullet Program Ref")
CFIELD(programId, "Program ID", "Bullet")
COMPONENT_META_END(BulletProgramRef)

COMPONENT_META_BEGIN(BulletStateComponent, "Bullet State")
CFIELD(speed, "Speed", "Bullet")
CFIELD(age, "Age", "Bullet")
CFIELD(lifetime, "Lifetime", "Bullet")
COMPONENT_META_END(BulletStateComponent)