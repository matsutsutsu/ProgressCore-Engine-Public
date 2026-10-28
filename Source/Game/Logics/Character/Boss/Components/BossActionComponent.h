#pragma once
// ===========================================================================
// 概要: ボス本体の動き（移動・後方ステップ・跳躍・突進）の
//       設定値と実行時の状態を持つ Component。
//       実行時の状態を書くのは BossActionSystem だけ。
// ===========================================================================
#include <SimpleMath.h>

// ボス本体が今やっている動き。
// 並びを変えたら BossActionComponentMeta.cpp の
// BossStateNames も同じ並びに直す。
enum class BossActionState : uint8_t
{
    None,     // 待機
    Move,     // 歩いて近づく
    Strafe,   // 横歩き
    BackStep, // 後方ステップ
    Leap,     // 跳躍
    Dash,     // 突進攻撃
};

// 跳躍の着地点の決め方
enum class BossLeapLanding : uint8_t
{
    AwayToCenter, // 中央を挟んで相手と反対側
    TowardTarget, // 相手の手前
    InPlace,      // その場
};

// 跳躍の段
// 並びを変えたら BossActionComponentMeta.cpp の BossLeapPhaseNames も直す
enum class BossLeapPhase : uint8_t
{
    Windup,   // 溜め
    Flight,   // 空中（滞空は頂点へ上がるまで）
    Hover,    // 頂点で停止（滞空だけ）
    Descent,  // 頂点から降りる（滞空だけ）
    Recovery, // 着地硬直
};

// 突進攻撃の段
enum class BossDashPhase : uint8_t
{
    Retreat,  // 後ろへ浮きながら下がる
    Aim,      // 空中で止まって狙う
    Windup,   // 構え。入った瞬間に方向を固定
    Charge,   // 地面へ下りながら突っ込む
    Travel,   // 着地後も走り続ける
    Stop,     // 止まる
    Recovery, // 硬直
};

// 後方ステップの段
enum class BossBackStepPhase : uint8_t
{
    Retreat,  // 下がり始め
    Stop,     // 下がりながら止まる
    Recovery, // 着地硬直
};

// 跳躍1種類分の跳び方
// 技の長さ ＝ 溜め＋空中＋頂点に留まる＋着地硬直
struct BossLeapDef
{
    float windup       = 0.4f;  // 溜め [秒]。LeapStart クリップより短く
    float flight       = 2.0f;  // 空中 [秒]。滞空は上がる＋降りるの合計
    float height       = 30.0f; // 頂点の高さ [m]
    float landRecovery = 0.5f;  // 着地硬直 [秒]
    float hover        = 0.0f;  // 頂点に留まる [秒]。0 なら留まらない
};

struct BossActionComponent
{
    // --- 移動（歩く速さは Jolt の walkSpeed）---

    float turnSpeed     = 8.0f; // 移動中・溜め中の向き直り
    float idleTurnSpeed = 4.0f; // 待機中の向き直り

    // --- 横歩き ---

    float strafeSpeed        = 3.0f;  // [m/s]
    float strafeTurnSpeed    = 6.0f;  // 回りながらの向き直り
    float strafeKeepDistance = 12.0f; // 保ちたい間合い [m]
    float strafeDistanceGain = 0.5f;  // 間合いを詰める強さ。0 で円

    // --- 後方ステップ ---

    // 技の長さ ＝ 下がり始め＋止まりのモーション（アニメから）＋着地硬直
    float backStepDistance    = 5.0f;  // 下がる距離 [m]
    float backStepHeight      = 1.2f;  // 浮く高さ [m]
    float backStepRetreatTime = 0.35f; // 下がり始めの長さ [秒]
    float backStepRecovery    = 0.25f; // 着地硬直 [秒]

    // --- 跳躍3種 ---

    BossLeapDef escapeLeap{0.25f, 1.1f, 40.0f, 0.55f}; // 中央へ逃げる
    float       escapeLandingRatio = 0.3f;              // 着地点の中心からの距離（半径比）

    BossLeapDef attackLeap{0.35f, 2.0f, 30.0f, 0.5f}; // 跳躍攻撃
    float       attackStandOff = 3.0f;                 // 相手の手前に降りる距離 [m]

    BossLeapDef hoverLeap{0.3f, 0.9f, 16.0f, 0.63f, 2.0f}; // 滞空

    // --- 突進攻撃 ---

    // 技の長さ ＝ 下がる＋狙う＋構え＋突っ込む（距離÷速さ）＋走り続ける＋止まる＋硬直
    float dashRetreatTime     = 0.5f;  // 下がる長さ [秒]
    float dashRetreatDistance = 6.0f;  // 下がる距離 [m]
    float dashRetreatHeight   = 3.0f;  // 浮く高さ [m]
    float dashAimTime         = 0.5f;  // 狙う長さ [秒]
    float dashWindupTime      = 0.4f;  // 構えの長さ [秒]
    float dashSpeed           = 45.0f; // 突っ込む速さ [m/s]
    float dashDistance        = 15.0f; // 突っ込む距離 [m]
    float dashTravelSpeed     = 25.0f; // 走り続ける速さ [m/s]
    float dashTravelTime      = 0.3f;  // 走り続ける長さ [秒]
    float dashStopTime        = 0.4f;  // 止まるまで [秒]
    float dashRecovery        = 0.5f;  // 硬直 [秒]

    // --- 実行時の状態（保存しない）---

    DirectX::SimpleMath::Vector3 moveDirection = DirectX::SimpleMath::Vector3::Zero; // 動いている向き（水平）
    BossActionState              currentState  = BossActionState::None;
    float                        actionTimer   = 0.0f; // 今の動きの経過 [秒]

    // 技の終わり。BT はこれを見て次の行動へ進む
    uint32_t actionSerial   = 0;     // 今の技の依頼番号（BossCommandComponent から写す）
    bool     actionFinished = false; // 今の技が硬直まで終わったか
    float    recoveryTimer  = 0.0f;  // 硬直の経過 [秒]
    float    lastActionTime = 0.0f;  // 前の技が終わるまでの長さ [秒]（調整の確認用）

    float strafeSign    = 1.0f;  // +1=右回り / -1=左回り
    bool  strafeFlipped = false; // 壁で反転済みか

    DirectX::SimpleMath::Vector3 lockedDirection = DirectX::SimpleMath::Vector3::UnitZ; // 技に入った瞬間に固定した進行方向

    // 以下、技の移動そのものは CharacterMovementComponent の予約が持つ

    // 跳躍
    BossLeapDef                  leapDef;                                            // 今の跳躍の跳び方
    BossLeapLanding              leapLanding    = BossLeapLanding::AwayToCenter;
    BossLeapPhase                leapPhase      = BossLeapPhase::Windup;
    DirectX::SimpleMath::Vector3 leapTakeoffPos = DirectX::SimpleMath::Vector3::Zero; // ドローンの陣形の高さに使う
    DirectX::SimpleMath::Vector3 leapLandingPos = DirectX::SimpleMath::Vector3::Zero;
    bool                         leapLandAttack = false; // 攻撃の着地クリップで受けるか

    // 後方ステップ
    BossBackStepPhase backStepPhase    = BossBackStepPhase::Retreat;
    float             backStepStopTime = 0.0f; // 止まりのモーションの長さ [秒]

    // 突進
    BossDashPhase                dashPhase      = BossDashPhase::Retreat;
    DirectX::SimpleMath::Vector3 dashStartPos   = DirectX::SimpleMath::Vector3::Zero; // 地面の高さに使う
    DirectX::SimpleMath::Vector3 dashAimPos     = DirectX::SimpleMath::Vector3::Zero;
    DirectX::SimpleMath::Vector3 dashEndPos     = DirectX::SimpleMath::Vector3::Zero;
    float                        dashChargeTime = 0.0f; // 突っ込む長さ [秒]
};
