/**
 * AIコマンド(脳)を受け取って動く、ボス本体の物理状態（筋肉）。
 */
#pragma once
#include <SimpleMath.h>

// ===================================================================================
// [ 列挙型 ] ボスの物理ステート（最小構成）
// ===================================================================================
enum class BossActionState : uint8_t
{
    None,  // 待機
    Move,  // プレイヤーへの移動中
    Melee, // 近接攻撃中（足を止めて斬る。将来 踏み込み/突進 で前進を足す）
};

// ===================================================================================
// [ 構造体 ] ボスアクション状態データ (POD)
// ===================================================================================
struct BossActionComponent
{
    // --- 調整パラメータ ---
    // 行動ごとの目標速度。最終的な実効速度は JoltCharacterConfigComponent::walkSpeed を上限にする。
    float walkSpeed     = 4.0f;  // 様子見・通常接近の歩行速度
    float runSpeed      = 8.0f;  // 距離を詰める走行速度（将来の行動用）
    float chargeSpeed   = 25.0f; // ダッシュ攻撃などの突進速度（将来の行動用）
    float turnSpeed     = 8.0f;  // 移動中の旋回速度
    float idleTurnSpeed = 4.0f;  // 待機中の旋回速度（ゆっくり睨む演出）
    float acceleration  = 8.0f;  // 加速の速さ
    float deceleration  = 10.0f; // 減速の速さ

    // --- ランタイム状態 ---
    float                        currentMoveSpeed = 0.0f;
    DirectX::SimpleMath::Vector3 moveDirection    = DirectX::SimpleMath::Vector3::Zero;

    BossActionState currentState = BossActionState::None;
};