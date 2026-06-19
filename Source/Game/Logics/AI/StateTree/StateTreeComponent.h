/// @file  StateTreeComponent.h
/// @brief Entity 1個分の「今どの状態にいるか」を保持する極小コンポーネント。
///
/// ★ DOD 絶対原則 ★
///   ポインタ・std::vector を一切持たない。
///   固定長バッファだけで構成し、Chunk に完全に乗ることを保証する。
///
/// メモリレイアウト (約 100 byte):
///   assetHandle(4) + pad(4)
///   activeStates[8](16) + activeDepth(1)
///   needsReselect(1) + pad(2)
///   scratch[64](64)
///
/// StateTreeAsset との役割分担:
///   StateTreeAsset  … 「どんな状態機械か」の設計図（全 Entity 共有・不変）
///   StateTreeComponent … 「今どの状態にいるか・インスタンスデータ」（Entity 固有）
#pragma once
#include <cstdint>
#include <cstring>  // memset

#include "Game/Logics/AI/StateTree/StateTreeAsset.h"   // StateTreeHandle


// ====================================================================
// StateTreeComponent
// ====================================================================

/// @brief AI の「今の状態」のみを保持する極小コンポーネント。
///
/// ECS ルールに従いグローバル空間に定義（プロジェクト規約）。
struct StateTreeComponent
{
    // ----------------------------------------------------------------
    // アセット参照
    // ----------------------------------------------------------------

    /// @brief どの StateTreeAsset を使うか（StateTreeRegistry のインデックス）。
    ///        スポーン時に設定し、以降は原則不変。
    CCL::AI::StateTreeHandle assetHandle;

    // ----------------------------------------------------------------
    // アクティブ状態パス
    // ----------------------------------------------------------------

    /// @brief 現在アクティブな状態の states インデックス列。
    ///
    /// [0] = ルート直下の選択状態、[1] = その子、... [activeDepth-1] = 末端葉。
    /// 例: Alive → Chase → AttackReach のとき
    ///     activeStates = {1, 3, 5, ...}, activeDepth = 3
    ///
    /// 深さ8で最大8階層。ボス級のフェーズ構成でも十分（ルート含む）。
    static constexpr int MAX_DEPTH = 8;
    uint16_t activeStates[MAX_DEPTH] = {};
    uint8_t  activeDepth = 0;

    // ----------------------------------------------------------------
    // 制御フラグ
    // ----------------------------------------------------------------

    /// @brief true のとき、次フレームで状態を選び直す。
    ///        初回 or 遷移発生後に StateTreeSystem が立てる。
    bool needsReselect = true;

    // ----------------------------------------------------------------
    // インスタンスデータ置き場（タイマー・キャッシュ等）
    // ----------------------------------------------------------------

    /// @brief タスクのインスタンスデータ（ITask::Instance 等）を置く固定長バッファ。
    ///
    /// ★ なぜ固定長か ★
    ///   std::vector にすると Chunk のポインタ連続性が壊れ、
    ///   ForEachParallel のキャッシュ効率が激減する（DOD の死）。
    ///
    /// ★ サイズ予算 (64 byte) ★
    ///   StateTreeAsset::instanceTotalSize がこれを超えていたら
    ///   StateTreeSystem の初期化時にエラーログを出してフォールバックする。
    ///   ボス級でも 64 byte で足りる設計にすること:
    ///     - 「経過時間 (float × 4)」+ 「状態フラグ (bool × 4)」= 20 byte が典型
    ///     - 複雑な状態は scratch ではなく別コンポーネントに逃がす
    static constexpr int SCRATCH_SIZE = 64;
    uint8_t scratch[SCRATCH_SIZE] = {};

    // ----------------------------------------------------------------
    // ヘルパー
    // ----------------------------------------------------------------

    /// @brief scratch 内の指定オフセットへのポインタを返す。
    ///        NodeRef::instanceOffset を渡して使う。
    void* GetInstancePtr(uint32_t offset)
    {
        if (offset + 0 >= SCRATCH_SIZE) return nullptr;
        return scratch + offset;
    }

    const void* GetInstancePtr(uint32_t offset) const
    {
        if (offset + 0 >= SCRATCH_SIZE) return nullptr;
        return scratch + offset;
    }

    /// @brief scratch 全体をゼロクリアする（状態リセット時）。
    void ClearScratch()
    {
        std::memset(scratch, 0, SCRATCH_SIZE);
    }

    /// @brief 現在末端状態のインデックスを返す。アクティブ状態がなければ ST_INVALID。
    uint16_t GetLeafState() const
    {
        if (activeDepth == 0) return CCL::AI::ST_INVALID;
        return activeStates[activeDepth - 1];
    }
};
