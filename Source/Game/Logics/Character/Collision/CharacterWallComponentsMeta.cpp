/// @file  CharacterCollisionComponentsMeta.cpp
/// @brief WallSlideTag / CharacterRadiusComponent の
///        シリアライズ/インスペクタ定義。
///        どちらもプレハブに付けてシーン保存する値。

#include "Game/Logics/Character/Collision/CharacterWallComponents.h"

#include "Engine/Serialization/Meta/ComponentDefine.h"

// ===========================================================================
// WallSlideTag
//   グリッドDDAによる壁押し出しを受けるキャラに付ける空タグ。
//   プレイヤー・敵・NPCなど「壁をすり抜けてはいけないもの」全般に使う。
// ===========================================================================
COMPONENT_META_BEGIN(WallSlideTag, "WallSlideTag")
// フィールドなし
COMPONENT_META_END(WallSlideTag)


// ===========================================================================
// CharacterRadiusComponent
//   壁スライド押し出しで使うキャラの水平当たり半径（XZ平面の円）。
//   Joltカプセルの半径と合わせるのが基本だが、
//   グリッド押し出し専用なので独立して微調整できる。
// ===========================================================================
COMPONENT_META_BEGIN(CharacterWallRadiusComponent, "CharacterRadius")
CFIELD_FLOAT_RANGED(radius, "Wall Slide Radius", 0.01f, 0.05f, 5.0f, "Collision")
COMPONENT_META_END(CharacterWallRadiusComponent)