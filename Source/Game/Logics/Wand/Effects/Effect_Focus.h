#pragma once
// ===========================================================================
// Effects/Effect_Focus.h
// ----------------------
// 役割:
//   アタッチメント「Focus(精密・一点突破)」の全フェーズ別ハンドラを集約する。
//   命名規則: <Name><Phase>。1アタッチメント=1ファイルに全フェーズを集める。
//
// 設計方針:
//   - Spawn: 扇角を絞り(spreadMult)・初速を上げる(speedMult)。乗算アキュムレータ。
//   - OnHit: 貫通フラグを立てる。壁=すり抜け / 敵=貫いて飛び続ける。
//     「精密＝一点を貫く」という概念が、Spawnでは収束、OnHitでは貫通として一貫する。
// ===========================================================================
#include "Game/Logics/Wand/Core/WandTypes.h"
#include "Game/Logics/Wand/Effects/SpawnResolution.h"
#include "Game/Logics/Wand/Effects/OnHitResolution.h"

/// @brief Spawn: 扇角を絞り、初速を上げる（どちらも乗算アキュムレータ）。
///        params[0]=扇角倍率(<1で絞る/0で真っ直ぐ), params[1]=初速倍率。
inline void FocusSpawn(const EffectOp& op, SpawnResolution& r)
{
    for (uint16_t s = 0; s < op.stackCount; ++s)
    {
        r.spreadMult *= op.params[0];
        r.speedMult *= op.params[1];
    }
}

/// @brief OnHit: 貫通フラグを立てる。壁=すり抜け / 敵=貫いて飛び続ける。
inline void FocusOnHit(const EffectOp& /*op*/, OnHitResolution& r)
{
    r.pierce = true;
}