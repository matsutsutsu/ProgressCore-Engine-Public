#pragma once
// ===========================================================================
// Effects/Effect_Focus.h
// ----------------------
// @file
// @brief アタッチメント「Focus(精密・一点突破)」のフェーズ別ハンドラ集約。
// ===========================================================================
#include "Game/Logics/Wand/Core/WandTypes.h"
#include "Game/Logics/Wand/Effects/SpawnResolution.h"

/// @brief Spawn: 精密フラグを立てるだけ。実効果(扇を絞る等)はシナジーで決まる。
inline void FocusSpawn(const EffectOp& op, SpawnResolution& r)
{
    // Focus は「弾の拡散を抑え、初速を上げる」ルーン。
    // params[0] = 0.0f (拡散率0倍), params[1] = 1.5f (初速1.5倍) と仮定
    r.spreadDeg *= op.params[0]; // これだけでSplitの拡散角度を強制的に0（真っ直ぐ）にできる！
    r.speedMult *= op.params[1];
}

// TODO(Step2): FocusOnHit(OnHitResolution&) … 貫通(pierce)はここに集約する。