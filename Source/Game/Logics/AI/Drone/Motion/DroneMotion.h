#pragma once
// ===========================================================================
// 概要: ドローンの状態ごとの動き方。DroneMovementSystem が状態に応じて呼ぶ。
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
// 備考:
//   動き方は3通りで固定なので、クラスに分けず switch で分ける。
//   足すときは ChargeMode に値を足し、.cpp の MoveCharge と IsChargeDangerous に case を足す。
//
//   各関数は drone.currentVelocity と trans.rotation を決める（予告の震えと剛体追従だけは位置も書く）。
//   位置への積分は DroneMovementSystem が後で行う。
//
//   補間係数は (std::min)(k * dt, 1.0f) で止める。1 を超えると目標を通り越す
//   （globalScale で dt が大きくなりうるので、dt 側では保証されない）。
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
        bool                         hasPlayer = false; // プレイヤーが居るか
    };

    // 実行中に DroneMovementSystem の調整 UI から変える値（World リソース）。きびきび具合は動かして目で見ないと決まらないため
    struct MotionTuning
    {
        float moveSpeedScale = 4.0f; // 持ち場へ戻る強いバネで出せる速さの上限（moveSpeed 倍）
        float farFromSlot    = 6.0f; // Idle でも持ち場からこれより遠い間は、強いバネで寄せる [m]
        float rigidCatchUp   = 4.0f; // 剛体追従で、持ち場にこれより近づいたら剛体に切り替える [m]
    };

    // ============================================================
    // 姿勢
    // ============================================================

    /**
     * @brief 進行方向から機体の姿勢を作る。モデルの前方は -Z。
     *
     * yaw は必ずこの関数で作る。+Z 前提の式と混ぜると、状態が切り替わるたびに機首が反転する。
     *
     * @param look         向かせたい方向。includePitch=true のときは正規化して渡す。
     * @param includePitch 上下の傾きも付けるか。false なら水平だけ向く。
     * @return 機体の姿勢。
     */
    DirectX::SimpleMath::Quaternion LookToRotation(const DirectX::SimpleMath::Vector3 &look, bool includePitch);

    /**
     * @brief 機首を look の向きへ少しずつ向け直す。
     * @param trans        向け直す機体の Transform。rotation を書く。
     * @param look         向かせたい方向。
     * @param includePitch 上下の傾きも付けるか。
     * @param turnRate     向け直す速さ [1/s]。
     * @param dt           この機体の経過時間 [秒]。
     */
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

    /**
     * @brief 動き方が使う World の値（MotionTuning と小隊のバネの強さ）を写し取る。
     * @param world ドローンが居る World。
     * @note 並列ループの前に1回だけ呼ぶ。ループの中で World を引かないため。
     */
    void BeginMotionFrame(CCL::ECS::World &world);

    // 突撃中の速度と姿勢を決める。drone.chargeMode で動き方を選ぶ
    void MoveCharge(TransformComponent &trans, DroneComponent &drone, float dt);

    /**
     * @brief 突撃中のこの機に、当たり判定を開けてよいかを返す。
     *
     * 動き方ごとの「当たらない区間」だけを答える（直進なら発射待ちとタメの間）。
     * DroneBrainSystem は「もう飛び出したか」の判定にも使う。
     *
     * @param drone 調べる機体。FireCharge 中であること。
     * @return 当たり判定を開けてよいなら true。
     */
    bool IsChargeDangerous(const DroneComponent &drone);

    /**
     * @brief 直進の突撃を始める。IFormation::OnChargeBegin の既定。
     *
     * 撃つ方向は、陣形が書いていればそれを使い、無ければプレイヤーへ向ける。
     * 方向はここで固定し、突撃中は変えない（ホーミングしない）。
     *
     * @param drone 突撃を始める機体。
     * @param info  プレイヤーの位置など。
     * @note FireCharge へ入った瞬間に1回だけ呼ぶ。
     */
    void BeginStraightCharge(DroneComponent &drone, const ChargeBeginInfo &info);

} // namespace CCL::AI::Motion
