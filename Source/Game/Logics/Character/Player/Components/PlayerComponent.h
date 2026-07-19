/// @file PlayerComponent.h
/// @brief プレイヤー固有のステータス（速度や設定）のみを保持するコンポーネント
#pragma once
#include "Engine/Serialization/Core/EntityLink.h"

struct PlayerComponent {

    float turnSpeed = 10.0f;

    // --- 追加：2段ジャンプ設定・状態 ---
    int  maxJumps             = 2;     ///< 最大ジャンプ可能回数 (2段なら2)
    int  currentJumpCount     = 0;     ///< 現在消費したジャンプ回数
    bool wasJumpButtonPressed = false; ///< 前フレームのボタン押下状態 (連打防止用)

    // =========================================================
    // 操作感のハック (Game Feel)
    // =========================================================
    float coyoteTimeWindow  = 0.15f; // コヨーテタイム許容時間 (秒)
    float jumpBufferWindow  = 0.15f; // 先行入力バッファ許容時間 (秒)
    float timeSinceGrounded = 0.0f;  // 地面から離れて経過した時間 (内部状態)
    float jumpBufferTimer   = 0.0f;  // ジャンプ入力からの経過時間 (内部状態)

    // =========================================================
    // 視覚的慣性 (バンキング)
    // =========================================================
    float bankingMultiplier = 0.5f;  // 傾きの強さ
    float maxBankingAngle   = 30.0f; // 最大傾斜角 (度)
    float currentYaw        = 0.0f;  // 純粋な進行方向 (ロジック用状態)
    float currentRoll       = 0.0f;  // 現在の傾き角 (描画用状態)

    // =========================================================
    // 着地シェイク用状態
    // =========================================================
    bool  wasGroundedLastFrame = true; // 前フレームの接地状態
    float lastFallVelocity     = 0.0f; // 直前のY速度（落下速度）

    // --- 攻撃入力状態 ---
    bool wasAttackButtonPressed = false; ///< 前フレームの攻撃ボタン押下状態 (連打・押しっぱなし防止用)

    // --- 攻撃時のターゲット自動フェイシング（オートエイム） ---
    //   攻撃入力の瞬間、PlayerAttackSystem が対象（ロックオン先／前方最寄り敵）方向の yaw を求めて
    //   attackFaceTimer を立てる。PlayerMoveSystem が >0 の間、移動より優先で currentYaw を敵へ回頭させる。
    float attackFaceYaw   = 0.0f; ///< 攻撃時に向くべき yaw(rad)
    float attackFaceTimer = 0.0f; ///< 自動フェイシングの残り時間(秒)。>0 の間だけ有効。

    // ロックオンデータ
    bool               lockOnToggleRequested = false;                     ///< Rボタンが押された瞬間だけ true になるトリガー意思
    CCL::Serialization::EntityLink lockOnTargetID  = CCL::ECS::InvalidEntityID; ///< 現在ロックオンしている敵のEntityID

};