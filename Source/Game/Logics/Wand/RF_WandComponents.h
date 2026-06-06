#pragma once
// ===========================================================================
// @file   RF_WandComponents.h
// @brief  杖エンティティが持つ2つのコンポーネントを定義する。
//           - WandBuildComponent : ショップで編集される「ビルド権威データ」
//           - WandComponent      : 戦闘中の発射ランタイム状態（クールダウン等）
//
// @note   設計方針: 「編集時データ」と「ランタイム状態」を別コンポーネントに
//         分離している。ビルド(WandBuildComponent)は不変寄り・低頻度変更、
//         発射状態(WandComponent)は毎フレーム変化、と性質が逆だからである。
//         分離により、ビルド変更時のみ走る「コンパイル」と、毎フレーム走る
//         「発射処理」を別システム・別ステージにきれいに割り当てられる。
// ===========================================================================

#include <vector>
#include "SimpleMath.h" // @note DirectXTK の SimpleMath。実環境のパスに合わせて調整。

#include "RF_PhaseTypes.h"

namespace RuneForge
{
    using Vector3 = DirectX::SimpleMath::Vector3;

    // ----------------------------------------------------------------------
    // WandBuildComponent（編集時データ＝ビルドの権威）
    // ----------------------------------------------------------------------

    /**
     * @brief 杖に装着されたアタッチメント構成（4フェーズ分）を保持する権威データ。
     *
     * 各フェーズのスロットは「装着順に並んだ AttachmentType の列」。
     * 重ねがけ（同フェーズに同種2個）は、同じ値が2回並ぶことで表現する。
     *
     * @code
     *   build.phases[PhaseIndex(Phase::Spawn)] = { Split, Split, Accelerate };
     *   // → 分裂×2 + 加速 の生成フェーズ
     * @endcode
     *
     * @note このコンポーネントが変化したら dirty を立てること。
     *       WandBuildCompileSystem が dirty を見てコンパイルし直す。
     * @note エディタ用GUI/シリアライズ（ComponentMeta特殊化=パターンB）は、
     *       ショップUI実装時に追加する。std::vector<enum> の編集は専用GUIが
     *       必要なため、コアループ確立を優先して現段階では純データに留める。
     * @warning このコンポーネントは「論理データ」であり、毎フレームの読み取りは
     *          しない。戦闘中の実処理はコンパイル済みの CompiledWandProgram を
     *          参照すること（フェーズ配列を毎フレーム走査しない）。
     */
    struct WandBuildComponent
    {
        /// @brief フェーズごとの装着列。添字は PhaseIndex(Phase::xxx)。
        std::vector<AttachmentType> phases[kPhaseCount];

        /// @brief 各フェーズの装着上限（ショップで解放して増やす）。
        std::uint8_t slotLimit[kPhaseCount] = { 3, 3, 3, 3 };

        /// @brief ビルドが変更され再コンパイルが必要なことを示すフラグ。
        ///        初期値 true: 生成直後に必ず1回コンパイルさせるため。
        bool dirty = true;
    };


    // ----------------------------------------------------------------------
    // WandComponent（戦闘中の発射ランタイム状態）
    // ----------------------------------------------------------------------

    /**
     * @brief 杖の発射制御（クールダウン）と、現在有効なコンパイル済みプログラム参照。
     *
     * 企画書仕様に従い「マナ／弾数の概念なし、杖のクールダウンのみ」で発射を制御する。
     *
     * @note fireRequested / aimDirection / muzzlePosition は、入力・エイム系の
     *       上流システム（L01_Input）が毎フレーム埋める想定の「入力窓口」。
     *       本システム群はこれを読むだけで、入力デバイスには関知しない（疎結合）。
     * @warning activeProgramId が InvalidProgramID の間は発射しない（未コンパイル）。
     */
    struct WandComponent
    {
        // --- 発射制御 ---
        float cooldownMax   = 0.40f; ///< 発射間隔（秒）
        float cooldownTimer = 0.0f;  ///< 残りクールダウン（0以下で発射可）

        // --- 弾の基礎パラメータ（アタッチメントが倍率で上書きする土台）---
        float baseBulletSpeed = 20.0f; ///< 基準速度（units/sec）
        float baseBulletLife  = 3.0f;  ///< 基準寿命（秒）。継承の発火タイミング。

        // --- コンパイル結果のキャッシュ ---
        ProgramID activeProgramId = InvalidProgramID; ///< 現ビルドのコンパイル済みID

        // --- 上流入力系が埋める窓口 ---
        bool    fireRequested = false;        ///< 今フレーム発射入力があるか
        Vector3 aimDirection  = { 0, 0, 1 };  ///< 照準方向（正規化済み想定）
        Vector3 muzzleOffset  = { 0, 0, 0 };  ///< 銃口オフセット（発射位置 = 杖位置 + これ）
    };

} // namespace RuneForge
