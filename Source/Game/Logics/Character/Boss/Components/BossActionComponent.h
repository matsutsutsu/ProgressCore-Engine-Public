#pragma once
// ===========================================================================
// 概要: ボス本体の動き（移動・横移動・後方ステップ・跳躍・突進）の
//       設定値と実行時の状態を持つ Component。
//       実行時の状態を書くのは BossActionSystem だけ。
// ===========================================================================
#include <SimpleMath.h>

// ボス本体が今やっている動き。
// 並びを変えたら BossActionComponentMeta.cpp の
// BossStateNames も同じ並びに直す。
enum class BossActionState : uint8_t
{
    None,       // 待機
    Move,       // 歩いて近づく
    SideTravel, // 横移動
    BackStep,   // 後方ステップ
    Leap,       // 跳躍
    Dash,       // 突進攻撃
    Footwork,   // 様子見（間合いを保ちながら動き回る）
};

// 様子見で動く向き。体の正面から見た前後左右（アニメの4方向）
// 並びを変えたら BossActionComponentMeta.cpp の BossFootworkDirNames も直す
enum class BossFootworkDir : uint8_t
{
    None,    // 様子見していない
    Forward, // 前
    Back,    // 後ろ
    Left,    // 左
    Right,   // 右
};

// 横移動の段
// 並びを変えたら BossActionComponentMeta.cpp の BossSideTravelPhaseNames も直す
enum class BossSideTravelPhase : uint8_t
{
    Start,  // 動き出し（速くなる）
    Travel, // 一定の速さで進む
    Stop,   // 止まる（遅くなる）
    End,    // 止まり終えた
};

// 跳躍の着地点の決め方
// 並びを変えたら BossActionComponentMeta.cpp の BossLeapLandingNames も直す
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

// 跳躍1種類分の跳び方。跳躍の技どうしの違いは、すべてここに持つ
// 技の長さ ＝ 溜め＋空中＋頂点に留まる＋着地硬直
struct BossLeapDef
{
    float           windup       = 0.4f;                          // 溜め [秒]。LeapStart クリップより短く
    float           flight       = 2.0f;                          // 空中 [秒]。滞空は上がる＋降りるの合計
    float           height       = 30.0f;                         // 頂点の高さ [m]
    float           landRecovery = 0.5f;                          // 着地硬直 [秒]
    float           hover        = 0.0f;                          // 頂点に留まる [秒]。0 なら留まらない
    BossLeapLanding landing      = BossLeapLanding::AwayToCenter; // 着地点の決め方
    bool            landAttack   = false;                         // 着地で攻撃する（判定のある着地クリップ）
};

struct BossActionComponent
{
    // --- 移動（歩く速さは Jolt の walkSpeed）---

    float turnSpeed     = 8.0f; // 移動中・溜め中の向き直り
    float idleTurnSpeed = 4.0f; // 待機中の向き直り

    // --- 横移動 ---

    // 動く距離は BT のノードの param が決める。技の長さは 距離÷速さ から決まる
    // 加速・減速の時間はアニメのクリップの長さと別。クリップは段の切り替えでブレンドして移る
    float sideTravelSpeed     = 90.0f; // 最高速 [m/s]
    float sideTravelAccelTime = 0.25f; // 0 から最高速までの時間 [秒]
    float sideTravelDecelTime = 0.35f; // 最高速から止まるまでの時間 [秒]

    // --- 間合いを取る ---

    // 動き方（速さ・加速・減速）は横移動の値を使う。動く距離は BT のノードの param
    float repositionNearDistance = 20.0f; // 相手がこれより近いと、離れながら回る [m]
    float repositionFarDistance  = 45.0f; // 相手がこれより遠いと、詰めながら回る [m]
    float repositionWallDistance = 20.0f; // 壁がこれより近いと、内側へ寄りながら回る [m]

    // --- 様子見 ---

    // 保ちたい距離・壁から寄る距離は「間合いを取る」の値を使う。1回の長さは下限〜上限からランダム
    float footworkSpeed   = 25.0f; // 動き回る速さ [m/s]
    float footworkMinTime = 1.5f;  // 1回の長さの下限 [秒]
    float footworkMaxTime = 3.0f;  // 1回の長さの上限 [秒]

    // --- 後方ステップ ---

    // 技の長さ ＝ 下がり始め＋止まりのモーション（アニメから）＋着地硬直
    float backStepDistance    = 5.0f;  // 下がる距離 [m]
    float backStepHeight      = 1.2f;  // 浮く高さ [m]
    float backStepRetreatTime = 0.35f; // 下がり始めの長さ [秒]
    float backStepRecovery    = 0.25f; // 着地硬直 [秒]

    // --- 跳躍3種 ---

    // 中央へ逃げる
    BossLeapDef escapeLeap{.windup       = 0.25f,
                           .flight       = 1.1f,
                           .height       = 40.0f,
                           .landRecovery = 0.55f,
                           .landing      = BossLeapLanding::AwayToCenter};
    float       escapeLandingRatio = 0.3f; // 着地点の中心からの距離（半径比）

    // 跳躍攻撃
    BossLeapDef attackLeap{.windup       = 0.35f,
                           .flight       = 2.0f,
                           .height       = 30.0f,
                           .landRecovery = 0.5f,
                           .landing      = BossLeapLanding::TowardTarget,
                           .landAttack   = true};
    float       attackStandOff = 3.0f; // 相手の手前に降りる距離 [m]

    // 滞空
    BossLeapDef hoverLeap{.windup       = 0.3f,
                          .flight       = 0.9f,
                          .height       = 16.0f,
                          .landRecovery = 0.63f,
                          .hover        = 2.0f,
                          .landing      = BossLeapLanding::InPlace};

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

    DirectX::SimpleMath::Vector3 lockedDirection = DirectX::SimpleMath::Vector3::UnitZ; // 技に入った瞬間に固定した進行方向

    // 以下、技の移動そのものは CharacterMovementComponent の予約が持つ

    // 跳躍
    BossLeapDef                  leapDef;                                            // 今の跳躍の跳び方（3種から写す）
    BossLeapPhase                leapPhase      = BossLeapPhase::Windup;
    DirectX::SimpleMath::Vector3 leapTakeoffPos = DirectX::SimpleMath::Vector3::Zero; // ドローンの陣形の高さに使う
    DirectX::SimpleMath::Vector3 leapLandingPos = DirectX::SimpleMath::Vector3::Zero;

    // 横移動
    BossSideTravelPhase sideTravelPhase      = BossSideTravelPhase::Start;
    bool                sideTravelLeft       = false; // 左へ動くか（アニメの左右を選ぶ）
    float               sideTravelCruiseTime = 0.0f;  // 最高速で進む長さ [秒]
    float               sideTravelStopTime   = 0.0f;  // 今回の減速の時間 [秒]。距離が短いと縮む

    // 様子見
    float           footworkTime    = 0.0f;                  // 今回の長さ [秒]（下限〜上限から引いた値）
    bool            footworkLeft    = false;                 // 相手の周りを左へ回っているか
    BossFootworkDir footworkAnimDir = BossFootworkDir::None; // アニメに渡す前後左右

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
