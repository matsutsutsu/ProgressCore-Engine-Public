#pragma once
// ===========================================================================
// WandRuntimeComponents.h
// -----------------------
// @file
// @brief 杖を「撃つ側」のエンティティ(プレイヤー/ボス等)が持つ実行時コンポーネント。
//        - WandRuntimeComponent  : クールダウン等の一時状態（非シリアライズ想定）
//        - WandFireIntentComponent: 入力Systemが埋める「発射意図」。発射ロジックと入力を分離する。
// ===========================================================================

#include "Game/Logics/Wand/Core/WandTypes.h"
#include "SimpleMath.h" // DirectX::SimpleMath::Vector3

/**
 * @brief 杖の実行時状態（クールダウン等）。authored 設定ではないのでシリアライズしない。
 */
struct WandRuntimeComponent
{
    float cooldownTimer = 0.0f; ///< 残りクールダウン秒。0以下で発射可能。
};

/**
 * @brief 入力/プレイヤーSystem が毎フレーム埋める「発射意図」。
 *
 * @note WandFireSystem はこれを読むだけ。入力デバイスや操作系の差異をここで吸収し、
 *       発射ロジックを入力実装から完全に独立させる（テスト・AI流用が容易になる）。
 */
struct WandFireIntentComponent
{
    bool wantFire = false;                               ///< 今フレーム発射したいか
    DirectX::SimpleMath::Vector3 aimDirection = { 0.0f, 0.0f, 1.0f }; ///< 照準方向（未正規化可）
    DirectX::SimpleMath::Vector3 muzzlePosition = {};    ///< 弾の発生位置（銃口・ワールド座標）
};