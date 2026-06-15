#pragma once
// ===========================================================================
// Effects/SpawnResolution.h
// -------------------------
// @file
// @brief Spawnフェーズの「解決結果」。各 <Name>Spawn ハンドラがここへ集約(Pass1)し、
//        Synergy_*(Pass2) が組合せ補正を加え、BulletSpawn がこれを消費して弾を撒く。
//
// @note 1発ごとに変わる値(将来のChaosジッタ等)はここには入れず、
//       SpawnOne 呼び出し直前で per-bullet に付与する（Resolveは純粋・決定的に保つ）。
// ===========================================================================
#include <cstdint>

struct SpawnResolution
{
    float speedMult = 1.0f;  ///< Accelerate/Slow が掛ける初速倍率
    int   volleyCount = 1;     ///< Split による同時発射本数
    float spreadDeg = 10.0f; ///< 扇の1発あたり角度[度]（Splitが設定）
};