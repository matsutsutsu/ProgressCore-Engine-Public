#pragma once
// ===========================================================================
// Effects/Effect_Accelerate.h
// ---------------------------
// @file
// @brief アタッチメント「Accelerate(速度・運動)」のフェーズ別ハンドラ集約。
//        命名規則: <Name><Phase>。このファイルに全フェーズ分を集める。
// ===========================================================================
#include "Game/Logics/Wand/Core/WandTypes.h"
#include "Game/Logics/Wand/Effects/SpawnResolution.h"

/// @brief Spawn: 初速倍率を掛ける。params[0]=倍率、重ねがけ分を累乗。
inline void AccelerateSpawn(const EffectOp& op, SpawnResolution& r)
{
    for (uint16_t s = 0; s < op.stackCount; ++s) r.speedMult *= op.params[0];
}

// TODO(Step2-3): AccelerateTravel(TravelResolution&) / AccelerateOnHit(OnHitResolution&)
//                を同じ命名規則でこのファイルに追加していく。