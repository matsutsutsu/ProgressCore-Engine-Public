#pragma once
// ===========================================================================
// WandBuildComponent.h
// --------------------
// @file
// @brief プレイヤーの杖に装着されているアタッチメント構成（オーサリング層）。
//        ショップでのみ編集され、変更時にコンパイルして ProgramID をキャッシュする。
//
// 設計上の位置づけ:
//   [WandBuildComponent] --compile--> [CompiledWandProgram] --register--> [Registry]
//        (編集可・永続化)                  (実行時不変)               (ProgramID 発行)
//
// @note 永続化対象（authored）。シリアライズ用 Meta は別途 .cpp に定義する（後工程）。
//       実行時の弾は本コンポーネントを持たず、ProgramID のみを保持する。
// ===========================================================================

#include "Game/Logics/Wand/Data/WandTypes.h"


/**
 * @brief 杖の装着構成。フェーズ×スロットに EffectType を並べた編集用データ。
 *
 * @note isDirty が true の間は未コンパイル。コンパイルSystem(後工程)が検知して
 *       Registry へ登録し cachedProgramId を更新し、isDirty を下ろす。
 */
struct WandBuildComponent
{
    /// @brief [フェーズ][スロット] の装着概念。None は空きスロット。
    EffectType slots[static_cast<int>(WandPhase::Count)][MAX_SLOTS_PER_PHASE] = {};

    /// @brief 各フェーズの有効スロット数（ショップ解放で増加）。
    uint8_t slotCounts[static_cast<int>(WandPhase::Count)] = { 0, 0, 0, 0 };

    float baseLifetime = 3.0f;  ///< 寿命(秒)
    float baseSpeed = 20.0f; ///< 基準初速
    float cooldown = 0.30f; ///< 発射クールダウン(秒)

    ProgramID cachedProgramId = InvalidProgramID; ///< コンパイル結果のキャッシュ
    bool      isDirty = true;             ///< 要再コンパイルフラグ
};
