#pragma once
// ===========================================================================
// Effects/Effect_Reflection.h
// ---------------------------
// 役割:
//   アタッチメント「Reflection(反射)」のフェーズ別ハンドラ集約。
//   OnHit フェーズで「壁に当たると跳ね返る」フラグを立てる。
//
// 設計方針:
//   - reflect は壁命中時のみ意味を持つ(敵命中では反射しない)。フラグだけ立て、
//     実際の反射ベクトル計算は壁System(法線が要る)が行う。
// ===========================================================================
#include "Game/Logics/Wand/Core/WandTypes.h"
#include "Game/Logics/Wand/Effects/OnHitResolution.h"

/// @brief OnHit: 反射フラグを立てる（壁で跳ね返る。実計算は壁System側）。
inline void ReflectionOnHit(const EffectOp& /*op*/, OnHitResolution& r)
{
    r.reflect = true;
}