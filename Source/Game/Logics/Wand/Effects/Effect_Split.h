#pragma once
// ===========================================================================
// Effects/Effect_Split.h
// ----------------------
// @file @brief 「Split(増殖・拡散)」のフェーズ別ハンドラ集約。
// ===========================================================================
#include "Game/Logics/Wand/Core/WandTypes.h"
#include "Game/Logics/Wand/Effects/SpawnResolution.h"

/// @brief Spawn: 本数と扇角を「足し算アキュムレータ」へ積む。
///        params[0]=1スタックあたり加算本数, params[1]=1発あたり扇角[度]。
inline void SplitSpawn(const EffectOp& op, SpawnResolution& r)
{
    r.bonusVolley += static_cast<int>(op.params[0] * op.stackCount);
    r.spreadAddDeg += op.params[1]; // 1発あたり角度。本数が増えれば総扇幅は自動で広がる
}

// TODO(後工程): SplitOnHit(着弾で分裂)