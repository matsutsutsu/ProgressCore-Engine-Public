#pragma once
// ===========================================================================
// Effects/Effect_Accelerate.h
// ---------------------------
// @file @brief 「Accelerate(速度・運動)」のフェーズ別ハンドラ集約。
// ===========================================================================
#include "Game/Logics/Wand/Core/WandTypes.h"
#include "Game/Logics/Wand/Effects/SpawnResolution.h"
#include "Game/Logics/Wand/Effects/TravelResolution.h"

/// @brief Spawn: 初速倍率を掛ける（掛け算アキュムレータ）。params[0]=倍率。
inline void AccelerateSpawn(const EffectOp& op, SpawnResolution& r)
{
    for (uint16_t s = 0; s < op.stackCount; ++s) r.speedMult *= op.params[0];
}

/// @brief Travel: 毎フレーム速度倍率を掛ける（乗算アキュムレータ）。
///        params[0] = 1秒あたりの加速率。dt を掛けてフレームレート非依存にする。
/// @param dt このフレームの localDt
inline void AccelerateTravel(const EffectOp& op, TravelResolution& r, float dt)
{
    for (uint16_t s = 0; s < op.stackCount; ++s)
        r.speedMult *= (1.0f + op.params[0] * dt);
}
