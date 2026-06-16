#pragma once
// ===========================================================================
// CompiledWandProgram.h
// ---------------------
// @file
// @brief オーサリング(WandBuildComponent)をコンパイルした「実行時不変」の弾プログラム。
//
// 役割:
//   - フェーズごとに実行すべき EffectOp 列を、重ねがけを畳んだ最終形で保持する。
//   - WandProgramRegistry(World Resource) に格納され、弾は ProgramID で参照する。
//   - 企画仕様「戦闘中ビルド変更不可」により、本構造体は戦闘中【完全不変】。
//     これにより ProgramID(=配列index) が安定し、弾は実体を共有せず安全に参照できる。
//
// @note 全フィールド固定長配列で POD（trivially-copyable）。std::vector を持たないため、
//       Registry の vector 内で再配置されても安全。コンパイルはショップ(非ホットパス)
//       でのみ走るので、固定長による若干のメモリ余剰は許容する。
// ===========================================================================

#include "Game/Logics/Wand/Core/WandTypes.h"


/**
 * @brief 1つの杖ビルドをコンパイルした実行時表現。
 * @warning 戦闘中に書き換えてはならない。書き換えはショップ遷移時のみ。
 */
struct CompiledWandProgram
{
    EffectOp spawnOps[MAX_OPS_PER_PHASE]        = {};
    EffectOp travelOps[MAX_OPS_PER_PHASE]       = {};
    EffectOp onHitOps[MAX_OPS_PER_PHASE]        = {};
    EffectOp inheritanceOps[MAX_OPS_PER_PHASE]  = {};

    uint8_t spawnCount       = 0; ///< spawnOps の有効数
    uint8_t travelCount      = 0; ///< travelOps の有効数
    uint8_t onHitCount       = 0; ///< onHitOps の有効数
    uint8_t inheritanceCount = 0; ///< inheritanceOps の有効数

    float baseDamage    = 10.0f; ///< 1ヒットの基礎ダメージ
    float baseKnockback = 0.0f;    // ノックバックの強さ（速度インパルス量）
    float baseCritRate  = 0.05f;   // クリティカル率（0.0〜1.0）
    float baseCritMult  = 2.0f;    // クリティカル時のダメージ倍率
    float baseLifetime  = 3.0f;  ///< 寿命(秒)。age がこれを超えると Inheritance 発動
    float baseSpeed     = 20.0f; ///< 基準初速
    float cooldown      = 0.30f; ///< 杖の発射クールダウン(秒)

    /**
     * @brief 指定フェーズの命令配列先頭と有効数を取得するヘルパ。
     * @param phase    取得対象フェーズ
     * @param outCount 有効命令数を受け取る出力引数
     * @return 命令配列の先頭ポインタ（不正フェーズなら nullptr）
     */
    const EffectOp* GetOps(WandPhase phase, uint8_t& outCount) const
    {
        switch (phase)
        {
        case WandPhase::Spawn:       outCount = spawnCount;       return spawnOps;
        case WandPhase::Travel:      outCount = travelCount;      return travelOps;
        case WandPhase::OnHit:       outCount = onHitCount;       return onHitOps;
        case WandPhase::Inheritance: outCount = inheritanceCount; return inheritanceOps;
        default:                     outCount = 0;                return nullptr;
        }
    }
};
