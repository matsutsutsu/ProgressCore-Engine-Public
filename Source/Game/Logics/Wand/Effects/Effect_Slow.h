#pragma once
// ===========================================================================
// Effects/Effect_Slow.h
// ---------------------
// 役割:
//   アタッチメント「Slow(減速・制動)」のフェーズ別ハンドラ集約。
//   Accelerate の対になる概念。Travel フェーズで速度倍率を下げる。
//
// 設計方針:
//   - Travel は毎フレーム適用。speedMult を 1 未満に掛けることで減速させる。
//     Accelerate(加速) と同じ乗算アキュムレータに積むため、両方装着すると
//     掛け合わされる(加速×減速=相殺)。順序非依存。
// ===========================================================================
#include "Game/Logics/Wand/Core/WandTypes.h"
#include "Game/Logics/Wand/Effects/TravelResolution.h"

/// @brief Travel: 毎フレーム速度倍率を下げる（乗算アキュムレータ）。
///        params[0] = 1秒あたりの減速率(正の値)。speed *= (1 - rate*dt)。
inline void SlowTravel(const EffectOp& op, TravelResolution& r, float dt)
{
    for (uint16_t s = 0; s < op.stackCount; ++s)
    {
        float f = 1.0f - op.params[0] * dt;
        if (f < 0.0f) f = 0.0f; // 行き過ぎて逆走しないようクランプ
        r.speedMult *= f;
    }
}