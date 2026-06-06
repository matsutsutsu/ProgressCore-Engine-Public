#pragma once
// ===========================================================================
// @file   RF_PhaseTypes.h
// @brief  RuneForge 弾システムの「フェーズ」「アタッチメント種別」「効果命令(EffectOp)」
//         といった最基層の型を定義する。エンジン非依存の純データ層であり、
//         弾コンポーネント／ビルド／コンパイル済みプログラム／各システムの
//         すべてがここに依存する（依存方向の最下流に置く意図）。
//
// @note   設計の核心:
//         「フェーズ × アタッチメント」の組み合わせは Archetype（型集合）では
//         なく、ここで定義する EffectOp の "データ列" で表現する。
//         これにより
//           1) 重ねがけ（同種2個=6発 等）が配列の重複として自然に表せる
//           2) ビルドごとの Archetype 爆発が起きない
//         を同時に満たす。詳細は RF_WandProgram.h を参照。
// ===========================================================================

#include <cstdint>

namespace RuneForge
{
    // ----------------------------------------------------------------------
    // プログラムID
    // ----------------------------------------------------------------------

    /// @brief コンパイル済み弾プログラム(CompiledWandProgram)を識別するID。
    ///        WandProgramRegistry 内の配列インデックスとして使う（O(1)参照）。
    using ProgramID = std::uint32_t;

    /// @brief 無効な ProgramID（全ビット1）。「継承スロットが空＝子なし」の表現にも使う。
    constexpr ProgramID InvalidProgramID = static_cast<ProgramID>(-1);


    // ----------------------------------------------------------------------
    // フェーズ（弾の一生のフック点）
    // ----------------------------------------------------------------------

    /**
     * @brief 弾のライフサイクル上の「効果発動タイミング」。
     *
     * フェーズは Component ではなく "イベント（フック点）" である点が最重要。
     *   - Spawn       : 発射した瞬間（弾数・初期方向・初速の決定）
     *   - Travel      : 飛行中、毎フレーム
     *   - OnHit       : 敵／壁に衝突した瞬間
     *   - Inheritance : 寿命が時間切れになった瞬間のみ（衝突消滅では発動しない）
     *
     * @note Count は配列サイズ確保用の番兵。enum 値として使用しないこと。
     */
    enum class Phase : std::uint8_t
    {
        Spawn = 0,
        Travel,
        OnHit,
        Inheritance,
        Count
    };

    /// @brief Phase をフェーズ配列の添字に変換するヘルパ。
    constexpr std::size_t PhaseIndex(Phase p) { return static_cast<std::size_t>(p); }

    /// @brief フェーズ総数（Spawn/Travel/OnHit/Inheritance = 4）。
    constexpr std::size_t kPhaseCount = static_cast<std::size_t>(Phase::Count);


    // ----------------------------------------------------------------------
    // アタッチメント種別
    // ----------------------------------------------------------------------

    /**
     * @brief アタッチメント（＝ルーン）の「概念」を識別する種別。
     *
     * 効果の中身はフェーズごとに異なる（同じ Accelerate でも Spawn と Travel で
     * 別挙動）。その対応付けは RF_AttachmentEffects で行い、ここでは
     * 「どの概念か」だけを保持する。
     *
     * @note 企画書の確定8種を先行定義。追加概念（Eternity/Gravity/Chain/
     *       Sacrifice 等）はこの末尾に足すだけで拡張できる（値の互換性のため
     *       途中挿入は避け、必ず末尾追加すること）。
     */
    enum class AttachmentType : std::uint8_t
    {
        Accelerate = 0, ///< 速度・運動
        Slow,           ///< 停滞・罠
        Focus,          ///< 精密・一点突破
        Homing,         ///< 追尾・執着
        Explosion,      ///< 範囲・拡散
        Split,          ///< 増殖・拡散
        Reflection,     ///< 対称性・反転
        Chaos,          ///< ランダム・賭け

        Count           ///< 番兵（種別総数）
    };


    // ----------------------------------------------------------------------
    // EffectOp（コンパイル済みの効果1個分）
    // ----------------------------------------------------------------------

    /**
     * @brief 1つの効果を表すランタイム命令。強化レベル等を解決済みのパラメータを持つ。
     *
     * 「ビルド（編集時データ）」を WandProgramRegistry が解決して並べた結果がこれ。
     * 重ねがけは EffectOp が同フェーズ配列に複数個並ぶことで表現される。
     *
     * @note p0 / p1 の意味はフェーズ×種別ごとに異なる（例: Accelerate の
     *       Spawn では p0=初速倍率、Travel では p0=毎秒加速量）。意味の対応は
     *       RF_AttachmentEffects.cpp の各 Apply 関数が単一の権威として管理する。
     */
    struct EffectOp
    {
        AttachmentType type = AttachmentType::Accelerate; ///< どの概念か
        float          p0   = 0.0f;                       ///< 主パラメータ
        float          p1   = 0.0f;                       ///< 副パラメータ
    };


    // ----------------------------------------------------------------------
    // BulletInit（Spawnフェーズが生成する「弾1発分の初期化情報」）
    // ----------------------------------------------------------------------

    /**
     * @brief Spawn フェーズ処理の出力。実体エンティティを作る直前の初期値。
     *
     * Split が弾数を増やし、Focus が拡散を絞り、Accelerate が初速倍率を上げる
     * …といった「発射時決定」はすべて BulletInit のリストへの加工として表現する。
     *
     * @note direction は未正規化で構わない（生成側で正規化する）。
     */
    struct BulletInit
    {
        float dirX = 0.0f, dirY = 0.0f, dirZ = 1.0f; ///< 進行方向（未正規化可）
        float speedMul = 1.0f;                        ///< 初速倍率（Accelerate等で加算/乗算）
    };

} // namespace RuneForge
