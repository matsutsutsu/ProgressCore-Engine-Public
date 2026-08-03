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

    // =========================================================
    // 空中状態（空中攻撃などの分岐に使う唯一の判定元）
    // =========================================================
    /// @brief 接地していない＝空中にいるか。PlayerMoveSystem が接地判定から毎フレーム更新する。
    ///        空中攻撃の作動条件など「空中かどうか」を見たいシステムはこのフラグを参照する。
    bool isAirborne = false;

    // --- 攻撃入力状態 ---
    bool wasAttackButtonPressed = false; ///< 前フレームの攻撃ボタン押下状態 (連打・押しっぱなし防止用)

    // --- 攻撃時のターゲット自動フェイシング（オートエイム） ---
    //   攻撃入力の瞬間、PlayerAttackSystem が対象（ロックオン先／前方最寄り敵）方向の yaw を求めて
    //   attackFaceTimer を立てる。PlayerMoveSystem が >0 の間、移動より優先で currentYaw を敵へ回頭させる。
    float attackFaceYaw      = 0.0f; ///< 攻撃時に向くべき yaw(rad)
    float attackFaceTimer    = 0.0f; ///< 自動フェイシングの残り時間(秒)。>0 の間だけ有効。
    float attackFaceMaxDistance = 5.0f; ///< この距離以内の敵にだけ攻撃時オートフェイシングする(m)。
                                        ///< 遠い敵（ロックオン中でも）には向かず、入力方向のまま攻撃する。

    // ロックオンデータ
    bool               lockOnToggleRequested = false;                     ///< Rボタンが押された瞬間だけ true になるトリガー意思
    CCL::Serialization::EntityLink lockOnTargetID  = CCL::ECS::InvalidEntityID; ///< 現在ロックオンしている敵のEntityID

};