#pragma once
// ===========================================================================
// 概要: プレイヤー固有の設定値と実行時の状態を持つ Component。
//
// 責務:
//   - 歩き・走り・回避の地上移動の状態と、走行の設定値を持つ
//   - 2段ジャンプの回数と、空中かどうかの判定を持つ
//   - 向き（yaw）と旋回時の傾き（roll）を持つ
//   - 攻撃時に敵の方へ向く処理と、ロックオン対象を持つ
//
// 備考: PlayerComponent を持つエンティティがプレイヤーとして扱われる
//       （CameraTargetBindSystem、PlayerAttackSystem の敵の判定など）。
// ===========================================================================
#include "Engine/Serialization/Core/EntityLink.h"

struct PlayerComponent
{
    // 地上移動の状態。書くのは PlayerDashSystem だけ。
    // 空中かどうかは isAirborne で別に持つ（地上移動の状態とは独立）。
    enum class GroundLocomotion : unsigned char
    {
        Walk,
        Run,
        Dodge
    };

    float turnSpeed = 10.0f; // 移動方向へ向き直る速さ（角度差に掛ける係数、1/秒）

    // =========================================================
    // 走行（Run）
    // =========================================================
    //   回避ダッシュを終えると走行に移り、以降は runSpeed で走り続ける。
    //   移動入力がある間は続け、無入力が runIdleReleaseTime 続いたら歩きに戻る。
    //   isRunning はダッシュ攻撃の条件にも使う（走っている間はダッシュ攻撃が出る）。
    float            runSpeed           = 24.0f;                  // 走行速度（m/秒）。walkSpeed と dashSpeed の間を目安にする
    float            runIdleReleaseTime = 0.4f;                   // 無入力がこの秒数続いたら歩きに戻す
    bool             isRunning          = false;                  // 走行中か
    float            runIdleTimer       = 0.0f;                   // 無入力が続いている時間（秒）
    GroundLocomotion locomotion         = GroundLocomotion::Walk; // 現在の地上移動の状態（アニメ・演出が参照する）

    // --- 2段ジャンプ ---
    //   押下の判定は PlayerInputComponent.jumpPressed で済んでいる。
    //   ここは何段目まで跳んだかだけを持つ。
    int maxJumps         = 2; // 着地までに跳べる回数
    int currentJumpCount = 0; // 着地してから跳んだ回数

    // =========================================================
    // 向きと旋回時の傾き（バンキング）
    // =========================================================
    float bankingMultiplier = 0.5f;  // 傾きの強さ
    float maxBankingAngle   = 30.0f; // 最大の傾き（度）
    float currentYaw        = 0.0f;  // キャラの向き（rad）。0 が +Z。物理にはこの向きだけを渡す
    float currentRoll       = 0.0f;  // 旋回時の傾き（rad）。見た目（Transform）にだけ反映する

    // =========================================================
    // 空中の判定
    // =========================================================
    // 空中にいるか。PlayerJumpFacingSystem が接地判定から毎フレーム書く。
    // 空中攻撃の分岐など、空中かどうかを見たいときはこのフラグを使う。
    bool isAirborne = false;

    // 接地を離れてからの経過時間（秒）。0=接地中。
    float timeSinceGrounded = 0.0f;

    // 接地を離れてからこの秒数までは空中とみなさない。段差を降りただけで
    // 空中攻撃が出たり地上攻撃が中断されたりしないようにする。
    // ジャンプできるかの判定には使わず、isAirborne が true になるのを遅らせるだけ（コヨーテタイムとは別）。
    float airborneGraceTime = 0.12f;

    // --- 攻撃時に敵の方へ向く処理 ---
    //   攻撃を押した瞬間に PlayerAttackSystem が対象（ロックオン先、なければ前方の最も近い敵）の yaw を求め、
    //   attackFaceTimer を立てる。
    //   attackFaceTimer が 0 より大きい間、PlayerJumpFacingSystem は移動方向より優先してこの yaw へ向ける。
    float attackFaceYaw         = 0.0f; // 向く先の yaw（rad）
    float attackFaceTimer       = 0.0f; // 向く処理の残り時間（秒）
    float attackFaceMaxDistance = 5.0f; // この距離（m）以内の敵にだけ向く。
                                        // 遠い敵にはロックオン中でも向かず、入力方向のまま攻撃する。

    // --- ロックオン ---
    //   切り替えの要求は PlayerInputComponent.lockOnPressed。ここは結果だけを持つ。
    CCL::Serialization::EntityLink lockOnTargetID = CCL::ECS::InvalidEntityID; // ロックオン中の敵
};
