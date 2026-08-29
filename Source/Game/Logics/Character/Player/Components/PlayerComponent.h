/// @file PlayerComponent.h
/// @brief プレイヤー固有のステータス（速度や設定）のみを保持するコンポーネント
#pragma once
#include "Engine/Serialization/Core/EntityLink.h"

struct PlayerComponent {

    /// @brief 地上ロコモーション状態。PlayerDashSystem が一元的に更新する。
    ///        空中かどうかは isAirborne が別管理（地上/空中は直交する軸）。
    enum class GroundLocomotion : unsigned char { Walk, Run, Dodge };

    float turnSpeed = 10.0f;

    // =========================================================
    // 走行（Run）状態：歩き⇔走りの二段ロコモーション
    //   回避（Dodge バースト）を終えると走行へ移行し、以降 runSpeed で走り続ける。
    //   移動入力がある限り維持し、無入力が runIdleReleaseTime 続いたら歩きへ自動解除。
    //   isRunning はダッシュ攻撃の作動条件にも使う（＝走っている間ずっとダッシュ攻撃が出せる）。
    // =========================================================
    float runSpeed           = 24.0f; ///< 走行状態の移動速度（walkSpeed と dashSpeed の中間が目安）
    float runIdleReleaseTime = 0.4f;  ///< 無入力がこの秒数続いたら走行を自動解除して歩きへ
    bool  isRunning          = false; ///< 走行状態か
    float runIdleTimer       = 0.0f;  ///< 無入力の継続時間（>=runIdleReleaseTime で解除）
    GroundLocomotion locomotion = GroundLocomotion::Walk; ///< 現在の地上ロコモーション（アニメ/演出参照用）

    // --- 2段ジャンプ設定・状態 ---
    //   押下の立ち上がり判定は PlayerInputComponent.jumpPressed が済ませている。
    //   ここが持つのは「何段目まで跳んだか」だけ。
    int maxJumps         = 2; ///< 最大ジャンプ可能回数 (2段なら2)
    int currentJumpCount = 0; ///< 現在消費したジャンプ回数

    // =========================================================
    // 視覚的慣性 (バンキング)
    // =========================================================
    float bankingMultiplier = 0.5f;  // 傾きの強さ
    float maxBankingAngle   = 30.0f; // 最大傾斜角 (度)
    float currentYaw        = 0.0f;  // 純粋な進行方向 (ロジック用状態)
    float currentRoll       = 0.0f;  // 現在の傾き角 (描画用状態)

    // =========================================================
    // 空中状態（空中攻撃などの分岐に使う唯一の判定元）
    // =========================================================
    /// @brief 接地していない＝空中にいるか。PlayerMoveSystem が接地判定から毎フレーム更新する。
    ///        空中攻撃の作動条件など「空中かどうか」を見たいシステムはこのフラグを参照する。
    bool isAirborne = false;

    /// @brief 接地を離れてからの経過時間(秒)。0=接地中。
    ///        小さな段差を降りた一瞬で空中扱いにしないためのグレース判定に使う。
    float timeSinceGrounded = 0.0f;

    /// @brief この秒数までの「接地離れ」は空中とみなさない猶予。段差を降りただけで
    ///        空中攻撃が暴発したり地上攻撃がキャンセルされるのを防ぐ（コヨーテとは別物：
    ///        ジャンプ成立判定には使わず、isAirborne の立ち上がりだけを遅らせる）。
    float airborneGraceTime = 0.12f;

    // --- 攻撃時のターゲット自動フェイシング（オートエイム） ---
    //   攻撃入力の瞬間、PlayerAttackSystem が対象（ロックオン先／前方最寄り敵）方向の yaw を求めて
    //   attackFaceTimer を立てる。PlayerMoveSystem が >0 の間、移動より優先で currentYaw を敵へ回頭させる。
    float attackFaceYaw      = 0.0f; ///< 攻撃時に向くべき yaw(rad)
    float attackFaceTimer    = 0.0f; ///< 自動フェイシングの残り時間(秒)。>0 の間だけ有効。
    float attackFaceMaxDistance = 5.0f; ///< この距離以内の敵にだけ攻撃時オートフェイシングする(m)。
                                        ///< 遠い敵（ロックオン中でも）には向かず、入力方向のまま攻撃する。

    // --- ロックオン ---
    //   切り替え要求は PlayerInputComponent.lockOnPressed。ここは結果だけを持つ。
    CCL::Serialization::EntityLink lockOnTargetID = CCL::ECS::InvalidEntityID; ///< 現在ロックオンしている敵のEntityID

};
