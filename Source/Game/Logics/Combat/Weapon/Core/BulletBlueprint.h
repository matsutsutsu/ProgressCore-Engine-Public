#pragma once
#pragma once
#include "Game/Logics/Combat/Weapon/Core/BulletEnums.h" // EffectID, BulletPhaseの定義が必要
#include <cmath>
#include <cstdint>

/* ==============================================================================
 * [CORE ARCHITECTURE: BulletBlueprint (データ指向設計)]
 * 役割: 1万発の弾丸で共有される「不変の設計図 (Flyweightパターン)」
 *
 * [ビットパッキング構造 (uint64_t 1ブロックあたり16枠)]
 * 64bitの整数1つに、16種類のアタッチメントの個数(各4bit: 最大15個)を隙間なく詰め込む。
 *
 *  [左側: 桁が大きい]                                                                    [右側: 桁が小さい] 
 * [15] [14] [13] [12] [11] [10] [ 9] [ 8] [ 7] [ 6] [ 5] [ 4] [ 3] [ 2] [ 1] [ 0]  (<- アタッチメントID)
 * 4b   4b   4b   4b   4b   4b   4b   4b   4b   4b   4b   4b   4b   4b   4b   4b   (計64bit・無駄ゼロ)
 *
 * 目的と効果:
 * 1. 極限のメモリ圧縮: 余りビットを完全に排除し、データ密度を100%に最適化。
 * std::map等によるヒープ確保とキャッシュミスを完全に排除する。
 * 2. 超高速参照: L1キャッシュに収まる極小のメモリフットプリントで、シフト演算(>>)と
 * マスク処理(&)のみによる1クロックでの最速ルックアップを実現する。
 * ============================================================================== */

struct BulletBlueprint {
    // -----------------------------------------------------------------------
    // ■ ビットパッキングによる効果カウント管理
    // -----------------------------------------------------------------------
    // 各効果に6ビット(0~63個)を割り当て、1つの変数で管理します。
    // メモリを節約し、キャッシュ効率を最大化します。

    // 1つの箱(64bit)に入るIDの数 = 64 / 4 = 16個
    static constexpr int ID_PER_BLOCK = 16;

    // 扱いたいエフェクトの最大種類数 (例: 50種類)
    static constexpr int MAX_EFFECT_TYPES = 20;

    // 必要な箱の数 (50 / 10 = 5個)
    static constexpr int BLOCK_COUNT = (MAX_EFFECT_TYPES + ID_PER_BLOCK - 1) / ID_PER_BLOCK;

    // ■ 配列に変更 (箱を複数用意)
    uint64_t spawnCounts[BLOCK_COUNT]  = {0};
    uint64_t travelCounts[BLOCK_COUNT] = {0};
    uint64_t onHitCounts[BLOCK_COUNT]  = {0};
    uint64_t endCounts[BLOCK_COUNT]    = {0};

    // シナジーフラグ（ビルド時に計算済み）
    uint32_t synergyFlags = 0;

    // -----------------------------------------------------------------------
    // ■ 基本パラメータ (ご要望の項目を追加)
    // -----------------------------------------------------------------------
    float baseDamage   = 10.0f; // 基本ダメージ
    float baseLifeTime = 3.0f;  // 基本寿命

    // クリティカル関連
    float criticalChance     = 0.1f; // クリティカル率 (0.0 ~ 1.0)
    float criticalMultiplier = 2.0f; // クリティカル時のダメージ倍率

    // ノックバック関連
    float knockbackPower = 1.0f; // ノックバックの強さ (メートル/秒 相当)

    // -----------------------------------------------------------------------
    // ■ 効果別パラメータ (ユニオン化も検討できますが、まずは構造体で明確に)
    // -----------------------------------------------------------------------
    struct Params {
        // Homing
        float homingStrength = 5.0f;  // 誘導力
        float homingRange    = 20.0f; // 索敵範囲

        // Accelerate
        float accelerateRate = 0.5f; // 加速率

        // Explosion
        float explosionRadius = 3.0f;  // 爆発半径
        float explosionDamage = 15.0f; // 爆発ダメージ

        // Split (分裂)
        int splitBaseCount = 3; // 分裂数

        // ... 他の効果パラメータ
    } params;

    // -----------------------------------------------------------------------
    // ■ ヘルパー関数 (ビット演算の隠蔽)
    // -----------------------------------------------------------------------
    static constexpr int      BITS_PER_EFFECT = 4;
    // 下位4ビットだけを取り出すためのマスク (1111 = 15)
    static constexpr uint64_t EFFECT_MASK     = 0b1111; // 15

    // 指定フェーズ・指定エフェクトの「個数」を取得する
    // これがシステム側から頻繁に呼ばれます
    
    // 暗号化された数字から、「指定した効果が何個あるか」を取り出す関数です。
    // システム側はこれを使って中身を確認します。
    int GetEffectCount(BulletPhase phase, EffectID effect) const {
        int id = static_cast<int>(effect);
        
        // 1. 「どの箱」に入ってるか？ (例: ID 13 なら 1番の箱)
        int blockIndex = id / ID_PER_BLOCK;
        
        // 2. 「箱の中のどこ」か？ (例: ID 13 なら 3番目の席)
        int subIndex = id % ID_PER_BLOCK;
        
        if (blockIndex >= BLOCK_COUNT) return 0; // 範囲外ガード

        const uint64_t* targetArray = nullptr;
        switch(phase) {
            case BulletPhase::Spawn:  targetArray = spawnCounts; break;
            case BulletPhase::Travel: targetArray = travelCounts; break;
            case BulletPhase::OnHit:  targetArray = onHitCounts; break;
            case BulletPhase::End:    targetArray = endCounts; break;
        }

        // 安全対策 (ここが警告の直接的な修正です)
        if (!targetArray) return 0;

        // 指定された箱(blockIndex)から取り出す
        int shift = subIndex * BITS_PER_EFFECT;

        // 箱から指定した位置までビットを右にずらすことで右側を消し
        // EFFECT_MASKで下位６ビットだけ1で埋まったものとAND演算（&）
        // をすることで指定したビットの個数だけを取り出すことができる
        return (targetArray[blockIndex] >> shift) & EFFECT_MASK;
    }
};