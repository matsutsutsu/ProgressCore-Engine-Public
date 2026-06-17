#pragma once
// ===========================================================================
// Effects/Effect_Focus.h
// ----------------------
// @file @brief 「Focus(精密・一点突破)」のフェーズ別ハンドラ集約。
// ===========================================================================
#include "Game/Logics/Wand/Core/WandTypes.h"
#include "Game/Logics/Wand/Effects/SpawnResolution.h"

/// @brief Spawn: 扇角を絞り、初速を上げる（どちらも「掛け算アキュムレータ」）。
///        params[0]=扇角倍率(<1で絞る/0で真っ直ぐ), params[1]=初速倍率。
/// @note  Splitが無ければ bonusVolley=0 → FinalVolleyCount()=1 となり扇角は無視される。
///        つまり「FocusがSplitを絞る」シナジーは合成式から自然に発生し、装着順に依存しない。
inline void FocusSpawn(const EffectOp& op, SpawnResolution& r)
{
    for (uint16_t s = 0; s < op.stackCount; ++s)
    {
        r.spreadMult *= op.params[0];
        r.speedMult *= op.params[1];
    }
}

// TODO(Step2): FocusOnHit(OnHitResolution&) … 貫通(pierce)はここに集約する。