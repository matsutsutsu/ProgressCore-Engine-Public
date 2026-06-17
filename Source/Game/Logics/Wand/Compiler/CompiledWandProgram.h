#pragma once
// ===========================================================================
// CompiledWandProgram.h
// ---------------------
// 役割:
//   オーサリング(WandBuildComponent)をコンパイルした「実行時不変」の弾プログラム。
//   フェーズ別の EffectOp 列(重ねがけ畳み込み済み)、弾の基礎ステータス、
//   弾に貼る見た目(VFX)の参照を保持する。
//
// 責務:
//   - フェーズ別命令列を最終形で保持し、GetOps で参照を提供する。
//   - 弾1発が持つ基礎値(ダメージ/ノックバック/クリティカル)と見た目VFXの唯一の置き場。
//     これらは「杖(プログラム)単位で全弾共通」であり、弾エンティティには複製しない。
//
// アーキテクチャ上の位置付け:
//   WandProgramRegistry(World Resource) に格納され、弾は ProgramID で参照する。
//   命中処理(HitboxCollisionSystem)は基礎戦闘値を、見た目付与(BulletVisualSystem)は
//   bulletVfxPath を、それぞれ programId からこの構造体を引いて読む。
//
// 設計方針(DOD):
//   - 全フィールド固定長配列・スカラのみで POD(trivially-copyable)。std::vector を
//     持たないため Registry の vector 内で再配置されても安全。
//   - 企画仕様「戦闘中ビルド変更不可」により戦闘中は完全不変。よって ProgramID
//     (=配列index)が安定し、弾は実体を共有せず安全に参照できる。
//   - 基礎値・VFXを弾側に複製しないのは、全弾同値(プログラム由来)であり 8192発ぶん
//     重複させるのがキャッシュ・メモリの無駄になるため(必要時にだけ引く)。
// ===========================================================================

#include "Game/Logics/Wand/Core/WandTypes.h"


/**
 * @brief 1つの杖ビルドをコンパイルした実行時表現。
 * @warning 戦闘中に書き換えてはならない。書き換えはショップ遷移時のみ。
 */
struct CompiledWandProgram
{
    EffectOp spawnOps[MAX_OPS_PER_PHASE] = {};
    EffectOp travelOps[MAX_OPS_PER_PHASE] = {};
    EffectOp onHitOps[MAX_OPS_PER_PHASE] = {};
    EffectOp inheritanceOps[MAX_OPS_PER_PHASE] = {};

    uint8_t spawnCount = 0; ///< spawnOps の有効数
    uint8_t travelCount = 0; ///< travelOps の有効数
    uint8_t onHitCount = 0; ///< onHitOps の有効数
    uint8_t inheritanceCount = 0; ///< inheritanceOps の有効数

    // --- 飛翔・寿命の基礎値 ---
    float baseLifetime = 3.0f;  ///< 寿命(秒)。age がこれを超えると Inheritance 発動
    float baseSpeed = 20.0f; ///< 基準初速
    float cooldown = 0.30f; ///< 杖の発射クールダウン(秒)

    // --- 戦闘の基礎値(命中時に HitboxCollisionSystem が参照) ---
    float baseDamage = 10.0f; ///< 基礎ダメージ(クリティカル前)
    float baseKnockback = 0.0f;  ///< ノックバックの強さ(0で無効)
    float baseCritRate = 0.05f; ///< クリティカル発生率 [0,1]
    float baseCritMult = 2.0f;  ///< クリティカル時のダメージ倍率

    // --- 見た目(飛翔中に貼るパーティクルVFXのPrefabパス。空なら見た目なし) ---
    char bulletVfxPath[128] = {}; ///< BulletVisualSystem が SpawnParticle に渡すJSONパス

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
