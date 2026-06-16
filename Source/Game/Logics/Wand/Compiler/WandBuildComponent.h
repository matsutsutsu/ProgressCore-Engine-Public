#pragma once
// ===========================================================================
// WandBuildComponent.h
// --------------------
// 役割:
//   プレイヤーの杖に装着されているアタッチメント構成(オーサリング層)。
//   ショップでのみ編集され、変更時にコンパイルして ProgramID をキャッシュする。
//
// 責務:
//   - フェーズ×スロットの EffectType 配置、および杖の基礎ステータス・見た目VFXの保持。
//   - これらは「編集可能・永続化対象」であり、コンパイルを経て CompiledWandProgram へ焼かれる。
//
// アーキテクチャ上の位置付け:
//   [WandBuildComponent] --compile--> [CompiledWandProgram] --register--> [Registry]
//        (編集可・永続化)                  (実行時不変)               (ProgramID 発行)
//   実行時の弾は本コンポーネントを持たず、ProgramID のみを保持する。
//
// 設計方針:
//   - 基礎戦闘値(ダメージ/ノックバック/クリティカル)と見た目VFXは「杖が持つ素の値」なので
//     オーサリング層であるここに置く。WandCompiler がそのまま CompiledWandProgram へ写す。
//   - POD 維持(固定長配列・スカラのみ)。永続化対象だが Meta は別途 .cpp に定義する。
// ===========================================================================

#include "Game/Logics/Wand/Core/WandTypes.h"
#include <string>

/**
 * @brief 杖の装着構成。フェーズ×スロットに EffectType を並べた編集用データ。
 *
 * @note isDirty が true の間は未コンパイル。コンパイルSystemが検知して Registry へ
 *       登録し cachedProgramId を更新し、isDirty を下ろす。
 */
struct WandBuildComponent
{
    /// @brief [フェーズ][スロット] の装着概念。None は空きスロット。
    EffectType slots[static_cast<int>(WandPhase::Count)][MAX_SLOTS_PER_PHASE] = {};

    /// @brief 各フェーズの有効スロット数（ショップ解放で増加）。
    uint8_t slotCounts[static_cast<int>(WandPhase::Count)] = { 0, 0, 0, 0 };

    // --- 飛翔・寿命の基礎値 ---
    float baseLifetime = 3.0f;  ///< 寿命(秒)
    float baseSpeed = 20.0f; ///< 基準初速
    float cooldown = 0.30f; ///< 発射クールダウン(秒)

    // --- 戦闘の基礎値（CompiledWandProgram へ焼かれ、命中時に参照される） ---
    float baseDamage = 10.0f; ///< 基礎ダメージ(クリティカル前)
    float baseKnockback = 0.0f;  ///< ノックバックの強さ(0で無効)
    float baseCritRate = 0.05f; ///< クリティカル発生率 [0,1]
    float baseCritMult = 2.0f;  ///< クリティカル時のダメージ倍率

    // --- 見た目（飛翔中に貼るパーティクルVFXのPrefabパス。空なら見た目なし） ---
    char bulletVfxPath[128] = {}; ///< コンパイル時に CompiledWandProgram へコピーされる

    ProgramID cachedProgramId = InvalidProgramID; ///< コンパイル結果のキャッシュ
    bool      isDirty = true;             ///< 要再コンパイルフラグ
};