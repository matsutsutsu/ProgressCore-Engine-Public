#pragma once
#include "Engine/Serialization/Core/EntityLink.h"

// プレイヤーの設定値と実行時の状態。これを持つエンティティがプレイヤーとして扱われる
struct PlayerComponent
{
    // 地上移動の状態。空中かどうかは isAirborne で別に持つ
    // 書くのは PlayerDashSystem（回避）と PlayerMoveSystem（歩き・走り）
    enum class GroundLocomotion : unsigned char
    {
        Walk,  // 歩き
        Run,   // 走り
        Dodge, // 回避ダッシュ
    };

    float turnSpeed = 10.0f; // 移動の向きへ向き直る速さ（1/秒）

    // --- 走り（回避ダッシュの後に移る）---

    float            runSpeed           = 24.0f;                  // 走る速さ（m/秒）
    float            runIdleReleaseTime = 0.4f;                   // 無入力がこの長さ続いたら歩きに戻す（秒）
    bool             isRunning          = false;                  // 走っているか。ダッシュ攻撃の条件にも使う
    float            runIdleTimer       = 0.0f;                   // 無入力が続いている長さ（秒）
    GroundLocomotion locomotion         = GroundLocomotion::Walk; // 今の地上移動の状態

    // --- 2段ジャンプ ---

    int maxJumps         = 2; // 着地までに跳べる回数
    int currentJumpCount = 0; // 着地してから跳んだ回数

    // --- 向きと旋回時の傾き ---

    float bankingMultiplier = 0.5f;  // 傾きの強さ
    float maxBankingAngle   = 30.0f; // 傾きの上限（度）
    float currentYaw        = 0.0f;  // 向き（rad）。0 が +Z。物理へはこれだけ渡す
    float currentRoll       = 0.0f;  // 旋回時の傾き（rad）。見た目にだけ付ける

    // --- 空中の判定（PlayerJumpFacingSystem が毎ステップ書く）---

    bool  isAirborne        = false; // 空中か。空中攻撃の分岐などはこれを見る
    float timeSinceGrounded = 0.0f;  // 接地を離れてからの長さ（秒）。0 は接地中
    float airborneGraceTime = 0.12f; // 接地を離れても、この長さまでは空中とみなさない（秒）

    // --- 攻撃時に敵へ向く（PlayerAttackSystem が書き、PlayerJumpFacingSystem が回す）---

    float attackFaceYaw         = 0.0f; // 向く先の yaw（rad）
    float attackFaceTimer       = 0.0f; // 向く残りの長さ（秒）。この間は移動の向きより優先
    float attackFaceMaxDistance = 5.0f; // この距離以内の敵にだけ向く（m）

    // --- ロックオン ---

    CCL::Serialization::EntityLink lockOnTargetID = CCL::ECS::InvalidEntityID; // ロックオン中の敵
};
