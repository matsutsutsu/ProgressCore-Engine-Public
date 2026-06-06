#pragma once
// ===========================================================================
// @file   RF_WandProgram.h
// @brief  「編集時のビルド(WandBuildComponent)」を、戦闘中に高速参照できる
//         「コンパイル済みプログラム(CompiledWandProgram)」へ変換し、
//         ID で集中管理する WandProgramRegistry（World の Resource）を定義する。
//
// @note   ここが本アーキテクチャの心臓部。狙いは3つ:
//           1) 発射を「ID コピー」だけにする（構造変更ゼロ、Archetype 爆発回避）
//           2) 重ねがけを EffectOp 配列の重複として表現する
//           3) 継承(Inheritance)で生まれる "子プログラム" を事前計算しておく
//              （継承スロットを空にした子を先に作る＝無限ループを構造的に防止）
//
// @warning スレッド安全性: コンパイル(Compile)はレジストリ(_programs)を書き換える。
//          一方 Travel 等のシステムは _programs を並列に読む。よってコンパイルは
//          並列システムが走らない専用ステージ（L01_Input の WandBuildCompileSystem）
//          でのみ実行すること。戦闘中（弾が飛んでいる間）のコンパイルは想定しない
//          ＝ビルド変更はショップでのみ起きる、というゲーム仕様と一致する。
// ===========================================================================

#include <vector>
#include <cstdint>

#include "RF_PhaseTypes.h"
#include "RF_WandComponents.h"

namespace RuneForge
{
    // ----------------------------------------------------------------------
    // 任意挙動コンポーネントのビットフラグ
    // ----------------------------------------------------------------------

    /**
     * @brief Spawn 時に弾へ追加する「任意挙動コンポーネント」を表すビット。
     * @note  これに該当する効果だけが専用システム＋専用コンポーネントを必要とする
     *        （毎フレーム重い処理）。それ以外は EffectOp データだけで完結する。
     */
    enum OptionalComponentBits : std::uint32_t
    {
        OPT_NONE           = 0,
        OPT_HOMING         = 1u << 0, ///< HomingState を付与
        OPT_PERIODIC_BLAST = 1u << 1, ///< PeriodicBlastTimer を付与
    };


    // ----------------------------------------------------------------------
    // CompiledWandProgram（戦闘中・不変のランタイム表現）
    // ----------------------------------------------------------------------

    /**
     * @brief ビルドを解決・平坦化した、戦闘中に参照される不変プログラム。
     *
     * - phases[Spawn/Travel/OnHit] : 各フェーズで適用する EffectOp 列（解決済み）
     * - phases[Inheritance]        : 使用しない（継承は childProgramId が表現する）
     * - childProgramId             : 寿命切れ時に生む子のプログラム。Invalid なら継承なし。
     * - optionalMask               : Spawn 時に付与する任意コンポーネントのビット和。
     *
     * @note 毎フレーム読むのは Travel の phases[Travel] だが、これは事前構築済み
     *       配列の "走査" であり動的確保は伴わない（最内ループでの new を回避）。
     */
    struct CompiledWandProgram
    {
        std::vector<EffectOp> phases[kPhaseCount];
        ProgramID             childProgramId = InvalidProgramID;
        std::uint32_t         optionalMask   = OPT_NONE;
    };


    // ----------------------------------------------------------------------
    // WandProgramRegistry（World の Resource として常駐）
    // ----------------------------------------------------------------------

    /**
     * @brief コンパイル済みプログラムを ID（=配列index）で集中管理するレジストリ。
     *
     * 同時に存在するアクティブなビルドはごく少数（プレイヤー1＋敵数種）なので、
     * 単純な vector での集中管理で十分かつ最速。
     *
     * @code
     *   // 起動時（Scene/Game 初期化）に1回:
     *   world.AddResource<RuneForge::WandProgramRegistry>();
     *   // ショップでビルド確定時（WandBuildCompileSystem 内）:
     *   wand.activeProgramId = reg.Compile(build);
     * @endcode
     *
     * @warning Compile はレジストリを再構築（Clear→積み直し）する設計。古い
     *          ProgramID は無効になるため、コンパイル後は必ず参照側
     *          (WandComponent.activeProgramId / BulletProgramRef.programId) を
     *          更新すること。戦闘中に飛行中の弾がいる状態での再コンパイルは禁止。
     */
    class WandProgramRegistry
    {
    public:
        /**
         * @brief ビルドをコンパイルし、親プログラムの ID を返す。
         *
         * 継承スロットが非空なら、先に「継承スロット→生成スロット、飛行/衝突は
         * 引き継ぎ、継承は空」とした子ビルドをコンパイルし、その ID を親に紐づける。
         *
         * @param build コンパイル対象のビルド
         * @return 親プログラムの ProgramID
         * @note   現状は単一ビルド前提でレジストリ全体を再構築する。複数の杖/敵を
         *         同時に持つ場合は「追記コンパイル＋世代管理」に発展させる（@todo）。
         */
        ProgramID Compile(const WandBuildComponent& build)
        {
            _programs.clear(); // @note 単一ビルド運用の暫定実装。将来は追記式に。

            // 1. 子プログラムを先に決定（継承スロットが空なら子なし＝ループ防止）
            ProgramID childId = InvalidProgramID;
            const auto& inheritSlot = build.phases[PhaseIndex(Phase::Inheritance)];
            if (!inheritSlot.empty())
            {
                // 子ビルド: 継承スロットの中身を「生成」へ移し、飛行/衝突は引き継ぐ。
                // 継承スロットは空のまま → この子はもう継承を起こさない（深さ1で停止）。
                WandBuildComponent childBuild;
                childBuild.phases[PhaseIndex(Phase::Spawn)]  = inheritSlot;
                childBuild.phases[PhaseIndex(Phase::Travel)] = build.phases[PhaseIndex(Phase::Travel)];
                childBuild.phases[PhaseIndex(Phase::OnHit)]  = build.phases[PhaseIndex(Phase::OnHit)];
                // childBuild.phases[Inheritance] は空 → childProgramId は Invalid になる

                childId = CompileSingle(childBuild, InvalidProgramID);
            }

            // 2. 親プログラムをコンパイルし、子を紐づける
            return CompileSingle(build, childId);
        }

        /**
         * @brief ID からコンパイル済みプログラムを参照する（O(1)）。
         * @warning 範囲外 ID はサニタイズせず未定義動作。呼び出し側で
         *          InvalidProgramID を弾いてから呼ぶこと。
         */
        const CompiledWandProgram& Get(ProgramID id) const { return _programs[id]; }

        /// @brief 登録数（デバッグ用）。
        std::size_t Size() const { return _programs.size(); }

        /// @brief 全消去（World::Clear 連動／ラン終了時など）。
        void Clear() { _programs.clear(); }

    private:
        /**
         * @brief 単一ビルドを1プログラムへ解決し、登録して ID を返す内部処理。
         * @param build   コンパイル対象
         * @param childId このプログラムの寿命切れ時に生む子（無ければ Invalid）
         */
        ProgramID CompileSingle(const WandBuildComponent& build, ProgramID childId)
        {
            CompiledWandProgram prog;
            prog.childProgramId = childId;

            // Spawn / Travel / OnHit のみ EffectOp 列へ解決する。
            // Inheritance は "効果列" ではなく childProgramId が表現するため積まない。
            const Phase execPhases[] = { Phase::Spawn, Phase::Travel, Phase::OnHit };
            for (Phase p : execPhases)
            {
                const std::size_t pi = PhaseIndex(p);
                prog.phases[pi].reserve(build.phases[pi].size());
                for (AttachmentType at : build.phases[pi])
                {
                    prog.phases[pi].push_back(ResolveOp(at, p));
                    prog.optionalMask |= OptionalMaskFor(at, p);
                }
            }

            _programs.push_back(std::move(prog));
            return static_cast<ProgramID>(_programs.size() - 1);
        }

        /**
         * @brief (種別, フェーズ) を解決済み EffectOp（パラメータ込み）へ変換する。
         *
         * @note これがアタッチメントの「数値バランス」の単一の権威。プロトでは
         *       Accelerate のみ実数を埋め、他はプレースホルダ。将来は JSON テーブル
         *       （既存の nlohmann::json パイプライン）から読み込む形に置換する（@todo）。
         */
        static EffectOp ResolveOp(AttachmentType at, Phase phase)
        {
            EffectOp op;
            op.type = at;

            switch (at)
            {
            case AttachmentType::Accelerate:
                switch (phase)
                {
                case Phase::Spawn:  op.p0 = 1.8f; break; // 初速1.8倍
                case Phase::Travel: op.p0 = 6.0f; break; // 毎秒 speedMul を +6
                case Phase::OnHit:  op.p0 = 1.2f; break; // 貫通時さらに1.2倍
                default: break;
                }
                break;

            // --- 以下はプロト段階では数値未確定（フェーズ配線の確認用） ---
            case AttachmentType::Slow:
            case AttachmentType::Focus:
            case AttachmentType::Homing:
            case AttachmentType::Explosion:
            case AttachmentType::Split:
            case AttachmentType::Reflection:
            case AttachmentType::Chaos:
            default:
                break;
            }
            return op;
        }

        /// @brief (種別, フェーズ) が必要とする任意コンポーネントのビットを返す。
        static std::uint32_t OptionalMaskFor(AttachmentType at, Phase phase)
        {
            if (at == AttachmentType::Homing && phase == Phase::Travel)
                return OPT_HOMING;
            if (at == AttachmentType::Explosion && phase == Phase::Travel)
                return OPT_PERIODIC_BLAST;
            return OPT_NONE;
        }

    private:
        std::vector<CompiledWandProgram> _programs; ///< ID(=index) で引く実体
    };

} // namespace RuneForge
