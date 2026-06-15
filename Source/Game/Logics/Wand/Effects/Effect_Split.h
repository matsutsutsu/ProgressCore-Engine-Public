#pragma once
// ===========================================================================
// Effects/Effect_Split.h
// ----------------------
// @file
// @brief アタッチメント「Split(増殖・拡散)」のフェーズ別ハンドラ集約。
// ===========================================================================
#include "Game/Logics/Wand/Core/WandTypes.h"
#include "Game/Logics/Wand/Effects/SpawnResolution.h"

inline void SplitSpawn(const EffectOp& op, SpawnResolution& r)
{
    // Databaseで決めた params[0](=2.0f) を使って、お盆(r)の弾数を増やす
    r.volleyCount += static_cast<int>(op.params[0] * op.stackCount);

    // Databaseで決めた params[1](=10.0f) を使って、お盆(r)の角度を広げる
    r.spreadDeg += op.params[1];
}

// TODO(後工程): SplitOnHit(着弾で分裂) を同ファイルに追加。