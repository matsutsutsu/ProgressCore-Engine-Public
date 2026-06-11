/// @file  BulletComponentsMeta.cpp
/// @brief 弾コンポーネントのメタ定義。
///        弾は実行時生成のためシーンには配置しないが、インスペクタでの値確認用として登録する。

#include "Game/Logics/Wand/Data/BulletComponents.h"
#include "Engine/Serialization/Meta/ComponentDefine.h"

// ===========================================================================
// BulletProgramRef
// ===========================================================================
COMPONENT_META_BEGIN(BulletProgramRef, "Bullet Program Ref")
CFIELD(programId, "Program ID", "Bullet")
COMPONENT_META_END(BulletProgramRef)

// ===========================================================================
// BulletStateComponent
// ===========================================================================
COMPONENT_META_BEGIN(BulletStateComponent, "Bullet State")
CFIELD(speed, "Speed", "Bullet")
CFIELD(age, "Age", "Bullet")
CFIELD(lifetime, "Lifetime", "Bullet")
COMPONENT_META_END(BulletStateComponent)