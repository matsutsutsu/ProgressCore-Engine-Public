/// @file CharacterMovementInputComponent.h
/// @brief キャラクターを動かすための「意思（ペダル）」を保持する共通コンポーネント
#pragma once
#include <SimpleMath.h>

struct CharacterMovementInputComponent {
    DirectX::SimpleMath::Vector3 desiredVelocity = { 0, 0, 0 }; // 移動要求ベクトル
    DirectX::SimpleMath::Vector3 lookTargetPos = { 0, 0, 0 };   // 振り向く先のワールド座標
    bool isAttackRequested = false;                           // 攻撃の意思
    bool jumpRequested = false;                               // ジャンプの意思

    bool dashRequested = false;  // 入力層からのDash要求
    float overrideSpeed = -1.0f; // アクション層からの速度上書き(-1.0fなら通常速度)

    // アクション層からの「3D速度まるごと上書き」。true の間は重力も水平計算も無視して
    // velocity3D をそのままキャラ速度に使う。切り払いのように「向いた方向へ上下含めて突進」
    // する技で使う（上を向いていれば上、下なら下へ飛ぶ）。
    bool                         use3DVelocity = false;
    DirectX::SimpleMath::Vector3 velocity3D    = {0.0f, 0.0f, 0.0f};

    // アクション層からの速度倍率(1.0で等倍)。walkSpeed/overrideSpeed の両方に掛かる。
    // 例: ジャスト回避チェインの段数に応じた移動速度上昇に使う。
    float speedMultiplier = 1.0f;


    // ルートモーション専用の通信領域
    bool                         isRootMotionActive = false;              // ルートモーション中か
    DirectX::SimpleMath::Vector3 rootMotionVelocity = {0.0f, 0.0f, 0.0f}; // アニメからの絶対移動速度

    // --- アクションの爽快感（慣性の引き継ぎ）用 ---
    DirectX::SimpleMath::Vector3 currentVelocity = {0, 0, 0};
};