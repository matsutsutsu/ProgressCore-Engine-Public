/**
 * @file BossActionComponent.h
 * @brief AIコマンド(脳)を受け取って動く、ボス本体の物理状態（筋肉）。
 *
 */
#pragma once
#include <SimpleMath.h>

// ===================================================================================
// [ 列挙型 ] ボスの物理ステート（最小構成）
// ===================================================================================
enum class BossActionState : uint8_t
{
    None, // 待機
    Move, // プレイヤーへの移動中
};

// ===================================================================================
// [ 構造体 ] ボスアクション状態データ (POD)
// ===================================================================================
struct BossActionComponent
{
    // --- 調整パラメータ ---
    float walkSpeed     = 4.0f;  // 移動時の最大速度
    float turnSpeed     = 8.0f;  // 移動中の旋回速度
    float idleTurnSpeed = 4.0f;  // 待機中の旋回速度（ゆっくり睨む演出）
    float acceleration  = 8.0f;  // 加速の速さ
    float deceleration  = 10.0f; // 減速の速さ

    // --- ランタイム状態 ---
    float                        currentMoveSpeed = 0.0f;
    DirectX::SimpleMath::Vector3 moveDirection    = DirectX::SimpleMath::Vector3::Zero;

    BossActionState currentState = BossActionState::None;
};