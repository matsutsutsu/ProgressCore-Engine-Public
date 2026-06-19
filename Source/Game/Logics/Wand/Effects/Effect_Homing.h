#pragma once
// ===========================================================================
// Effects/Effect_Homing.h
// -----------------------
// 役割:
//   アタッチメント「Homing(誘導・追尾)」のフェーズ別ハンドラ集約。
//   Travel フェーズで弾の進行方向を目標(最寄りの敵)へ補間する。
//
// 設計方針:
//   - ハンドラは純粋に保つ。敵探索(World/Resource依存)は System 側で行い、ここには
//     確定済みの「目標方向」を渡す。ハンドラは TravelResolution へ steer 要求を積むだけ。
//   - 追尾の強さは params[0] を基準に、dt とスタック数で旋回率を決める。複数 Homing は
//     RequestSteer の max 採用により最も強い旋回率が採られる(順序非依存)。
// ===========================================================================
#include "Game/Logics/Wand/Core/WandTypes.h"
#include "Game/Logics/Wand/Effects/TravelResolution.h"

/// @brief Travel: 目標方向へ向き補間を要求する（steerアキュムレータ）。
///        params[0] = 1秒あたりの旋回率(大きいほど急に追尾)。
/// @param op        Homing の EffectOp（stackCount=装着数）
/// @param targetDir System が確定した目標方向（正規化想定。XZ）
/// @param r         積み先の TravelResolution
/// @param dt        このフレームの localDt
inline void HomingTravel(const EffectOp& op,
    const DirectX::SimpleMath::Vector3& targetDir,
    TravelResolution& r, float dt)
{
    // 旋回率 = params[0] * dt * stack。1.0でクランプ（行き過ぎ防止）。
    float lerp = op.params[0] * dt * static_cast<float>(op.stackCount);
    if (lerp > 1.0f) lerp = 1.0f;
    r.RequestSteer(targetDir, lerp);
}