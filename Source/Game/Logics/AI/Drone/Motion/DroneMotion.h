#pragma once
// ===========================================================================
// 概要: ドローンの状態ごとの動き方。DroneMovementSystem が状態に応じて呼ぶ
//
// 責務:
//   - 持ち場へ戻る動き（MoveToTarget）と、持ち場を保つ動き（Idle）
//   - 突撃中（FireCharge）の動き。技（IFormation）が書いた drone.chargeMode で3通りに分ける
//       Straight    … 弾幕。タメ → 直進
//       RigidFollow … データ陣形。持ち場に毎フレーム一致させ、形が歪まない
//       SoftFollow  … 小隊突撃。持ち場へバネで寄り、隊列がしなる
//   - 突撃中に当たり判定を開けてよいか
//   - 進行方向から姿勢を作る
//
// 備考: 動き方は3通りで固定なので、クラスに分けず switch で分ける。足すときは ChargeMode と .cpp の case に足す
//       各関数は速度と回転を決め、位置への積分は DroneMovementSystem が行う（予告の震えと剛体追従だけは位置も書く）
//       補間係数は (std::min)(k * dt, 1.0f) で止める。globalScale で dt が大きくなると、1 を超えて目標を通り越す
// ===========================================================================
#include "SimpleMath.h"

#include "Engine/Transform/Components/TransformComponent.h"
#include "Game/Logics/AI/Drone/Components/DroneComponent.h"

namespace CCL::ECS { class World; }

namespace CCL::AI::Motion
{
    // 突撃を始めるときに技へ渡す情報（IFormation::OnChargeBegin）
    struct ChargeBeginInfo
    {
        DirectX::SimpleMath::Vector3 playerPos = {};    // プレイヤーの位置（ワールド）
        bool                         hasPlayer = false; // プレイヤーがいるか
    };

    // 実行中に DroneMovementSystem の調整 UI から変える値（World リソース）
    // きびきび具合は動かして見ないと決まらないので、実行中に変えられるようにしてある
    struct MotionTuning
    {
        float moveSpeedScale = 4.0f; // 持ち場へ戻る強いバネで出せる速さの上限（moveSpeed 倍）
        float farFromSlot    = 20.0f; // Idle でも持ち場からこれより遠い間は、強いバネで寄せる [m]
        float rigidCatchUp   = 20.0f; // 剛体追従で、持ち場にこれより近づいたら剛体に切り替える [m]
    };

    // ============================================================
    // 姿勢
    // ============================================================

    // 進行方向から機体の姿勢を作る。モデルの前方は -Z。includePitch なら look は正規化して渡す
    // yaw は必ずこの関数で作る。+Z 前提の式と混ぜると、状態が切り替わるたびに機首が反転する
    DirectX::SimpleMath::Quaternion LookToRotation(const DirectX::SimpleMath::Vector3 &look, bool includePitch);

    // 機首を look の向きへ、turnRate（1/秒）の速さで少しずつ向け直す
    void TurnToward(TransformComponent &trans, const DirectX::SimpleMath::Vector3 &look, bool includePitch,
                    float turnRate, float dt);

    // ============================================================
    // 持ち場へ戻る・持ち場を保つ
    // ============================================================

    // 持ち場へ戻る（MoveToTarget）。Idle より強いバネで寄せる
    void MoveToSlot(TransformComponent &trans, DroneComponent &drone, float dt);

    // 持ち場を保つ（Idle）。弱いバネ（持ち場から遠い間は強いバネ）に、ホバーの揺れと予告の震えを足す
    void HoldSlot(TransformComponent &trans, DroneComponent &drone, float dt);

    // ============================================================
    // 突撃（FireCharge）
    // ============================================================

    // 動き方が使う World の値（MotionTuning と小隊のバネの強さ）を写し取る
    // 並列ループの前に1回だけ呼ぶ。ループの中で World を引かないため
    void BeginMotionFrame(CCL::ECS::World &world);

    // 突撃中の速度と姿勢を決める。drone.chargeMode で動き方を選ぶ
    void MoveCharge(TransformComponent &trans, DroneComponent &drone, float dt);

    // 突撃中（FireCharge）のこの機に、当たり判定を開けてよいかを返す
    // 動き方ごとの当たらない区間（直進なら発射待ちとタメ）だけを答える
    // DroneBrainSystem は、もう飛び出したかの判定にも使う
    bool IsChargeDangerous(const DroneComponent &drone);

    // 直進の突撃を始める（IFormation::OnChargeBegin の既定）。FireCharge へ入った瞬間に1回だけ呼ぶ
    // 撃つ方向は、陣形が書いていればそれ、なければプレイヤーへ向け、突撃中は変えない
    void BeginStraightCharge(DroneComponent &drone, const ChargeBeginInfo &info);

}
