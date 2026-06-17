#pragma once
// ===========================================================================
// Effects/SpawnResolution.h
// -------------------------
// @file
// @brief Spawnフェーズの「解決結果」。各 <Name>Spawn ハンドラがここへ積む。
//
//   設計の肝：
//     ・掛け算で効くもの → *Mult アキュムレータ（初期値1.0、各ハンドラが *=）
//     ・足し算で効くもの → *Add / bonus アキュムレータ（初期値0、各ハンドラが +=）
//   足し算同士・掛け算同士は可換なので【装着順に依存しない】。
//   最終値は Final*() が一元的な式で合成する。シナジーはこの式から自然に出る
//   （例：Focusが spreadMult を下げ、Splitが spreadAddDeg を上げる → 自動で「絞れた扇」）。
// ===========================================================================
#include <cstdint>

struct SpawnResolution
{
    // --- 掛け算アキュムレータ（順序非依存）---
    float speedMult = 1.0f; ///< Accelerate / Focus が掛ける初速倍率
    float spreadMult = 1.0f; ///< Focus が掛ける扇角倍率（<1で絞る）

    // --- 足し算アキュムレータ（順序非依存）---
    int   bonusVolley = 0;    ///< Split が足す本数
    float spreadAddDeg = 0.0f; ///< Split が設定する1発あたり扇角[度]

    // --- 最終確定（合成順序をここで一元定義）---
    int   FinalVolleyCount()             const { return 1 + bonusVolley; }
    float FinalSpreadDeg()               const { return spreadAddDeg * spreadMult; }
    float FinalSpeed(float baseSpeed)    const { return baseSpeed * speedMult; }
};