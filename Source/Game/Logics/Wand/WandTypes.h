#pragma once
// ===========================================================================
// RuneForge_WandTypes.h
// ---------------------
// @file
// @brief RuneForge 杖システムの最下層データ定義（共通ボキャブラリ）。
//        フェーズ(WandPhase)・効果概念(EffectType)・単一命令(EffectOp)・
//        プログラム識別子(ProgramID) を集約する。上位の全レイヤがこれに依存する。
//
// 設計意図:
//   弾の多様性を【Archetype(型構造)】ではなく【データ(命令列)】で表現するための土台。
//   ここに定義する型は全て POD / trivially-copyable とし、Chunk への配置・
//   Registry への格納・memcpy コピーが安全に行えることを保証する。
//
// @note アタッチメント = ルーン = 「概念(EffectType)」。同じ概念でも装着フェーズで
//       異なる挙動になる（その解釈はハンドラ側の責務であり、データ側は不変）。
// ===========================================================================

#include <cstdint>

namespace RuneForge
{
    /**
     * @brief 弾の一生における4つのフェーズ（効果発動タイミング）。
     * @note 配列インデックスとして使うため 0 始まり連番を厳守する。Count は要素数。
     */
    enum class WandPhase : uint8_t
    {
        Spawn = 0, ///< 発射した瞬間（WandFireSystem が1回だけ実行）
        Travel = 1, ///< 飛行中（BulletTravelSystem が毎フレーム実行）
        OnHit = 2, ///< 衝突した瞬間（衝突解決System が実行）
        Inheritance = 3, ///< 寿命切れ(時間切れのみ)。新弾の Spawn 命令として焼き直す
        Count = 4
    };

    /**
     * @brief アタッチメント（ルーン）の「概念」を表す命令コード（オペコード）。
     *
     * 1概念 = 1オペコード。フェーズごとの挙動差は、この値を解釈するハンドラ側が
     * switch(type) で振り分けて実装する。新ルーン追加は、ここに1行・ハンドラに
     * case を1つ足すだけで完結する（Archetype は不変のまま）。
     *
     * @warning 値はシリアライズおよび継承時のコピーで保存されるため、既存メンバの
     *          番号を後から変更してはならない（追加は必ず末尾に行うこと）。
     */
    enum class EffectType : uint16_t
    {
        None = 0,
        Accelerate = 1, ///< 速度・運動
        Slow = 2, ///< 停滞・罠
        Focus = 3, ///< 精密・一点突破
        Homing = 4, ///< 追尾・執着
        Explosion = 5, ///< 範囲・拡散
        Split = 6, ///< 増殖・拡散
        Reflection = 7, ///< 対称性・反転
        Chaos = 8, ///< ランダム・賭け
        // --- 追加予定（番号は予約。必ず末尾追加） ---
        // Eternity, Gravity, Chain, Sacrifice, Burn, Freeze, Poison ...
        Count
    };

    /**
     * @brief コンパイル済みプログラムの識別子。Registry の配列インデックスそのもの。
     * @note 弾はこの軽量 ID だけを持ち、プログラム実体(CompiledWandProgram)は共有しない。
     */
    using ProgramID = uint32_t;

    /// @brief 無効な ProgramID。
    inline constexpr ProgramID InvalidProgramID = 0xFFFFFFFFu;

    /// @brief 1フェーズあたりに保持できる「異なる概念」の最大数。
    /// @note 重ねがけ(同概念の複数装着)は EffectOp::stackCount に畳むため、スロット解放で
    ///       この数を超えることは基本的に無い（概念の種類数で頭打ちになる）。
    inline constexpr uint32_t MAX_OPS_PER_PHASE = 16;

    /// @brief 1フェーズあたりの装着スロット上限（オーサリング側・ショップで解放）。
    inline constexpr uint32_t MAX_SLOTS_PER_PHASE = 8;

    /**
     * @brief 1つの効果命令（コンパイル後）。フェーズ実行ループで解釈される最小単位。
     *
     * メモリレイアウト（20 byte, POD）:
     * @code
     *   +0  type        (2B)  EffectType
     *   +2  stackCount  (2B)  重ねがけ数（同概念の複数装着を畳んだ数）
     *   +4  params[0..3](16B) 概念ごとの調整値（速度倍率・半径・分裂数など）
     * @endcode
     *
     * @note ハンドラは (type, stackCount, params) だけで挙動を決め、外部状態を参照しない
     *       純粋関数的な実装を保つ。stackCount の解釈(乗算か加算か)はハンドラの裁量。
     */
    struct EffectOp
    {
        EffectType type = EffectType::None;
        uint16_t   stackCount = 1;
        float      params[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    };

} // namespace RuneForge