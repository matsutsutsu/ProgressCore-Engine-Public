/// @file  StateTreeComponent.h
/// @brief Entity 1個分の「今どの状態にいるか」を保持する極小コンポーネント。
///
/// assetName（文字列）を保存対象にし、実行時に StateTreeRegistry で
/// StateTreeHandle に解決する。プレハブの JSON に
///   "assetName": "Enemy"
/// と書くだけで、スポーン後に自動でハンドルが設定される。
#pragma once
#include <cstdint>
#include <string>

#include "Game/Logics/AI/StateTree/Core/StateTreeAsset.h"


/// @brief AI の「今の状態」のみを保持する極小コンポーネント。
struct StateTreeComponent
{
    // ----------------------------------------------------------------
    // 保存対象：アセット名（プレハブ JSON に書く値）
    // ----------------------------------------------------------------
    /// @brief StateTreeRegistry に登録されたアセット名。
    ///        プレハブ保存時にここが書き出され、ロード時に Handle に解決される。
    ///        例: "Enemy" / "Boss" / "Goblin"
    std::string assetName;

    // ----------------------------------------------------------------
    // 実行時専用：Handle（保存しない）
    // ----------------------------------------------------------------
    /// @brief StateTreeRegistry から解決済みのハンドル。
    ///        Meta の Load 処理で assetName → Handle に変換される。
    CCL::AI::StateTreeHandle assetHandle;

    // ----------------------------------------------------------------
    // アクティブ状態パス（実行時専用・保存しない）
    // ----------------------------------------------------------------
    static constexpr int MAX_DEPTH = 8;
    uint16_t activeStates[MAX_DEPTH] = {};
    uint8_t  activeDepth = 0;

    // ----------------------------------------------------------------
    // ヘルパー
    // ----------------------------------------------------------------
    uint16_t GetLeafState() const
    {
        if (activeDepth == 0) return CCL::AI::ST_INVALID;
        return activeStates[activeDepth - 1];
    }
};