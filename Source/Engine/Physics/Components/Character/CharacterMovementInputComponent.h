/// @file CharacterMovementInputComponent.h
/// @brief キャラクターを動かすための「意思（ペダル）」を保持する共通コンポーネント
#pragma once
#include <SimpleMath.h>

struct CharacterMovementInputComponent {
    DirectX::SimpleMath::Vector3 desiredVelocity = { 0, 0, 0 }; // 移動要求ベクトル
    DirectX::SimpleMath::Vector3 lookTargetPos = { 0, 0, 0 };   // 振り向く先のワールド座標
    bool isAttackRequested = false;                           // 攻撃の意思
    bool jumpRequested = false;                               // ジャンプの意思（今回は不使用だが維持）
};