#pragma once
// ===========================================================================
// 概要: ドローン個体の判断層。DroneComponent.currentState を毎フレーム決める System。
//
// 責務:
//   - 硬直・突撃・離脱・接近・待機のうち、優先度順に最初に成立したものを選ぶ
//   - 突撃の開始（方向の固定）と、個別寿命の突撃の終了判断を行う
//   - 吹き飛ばし・立て直しのタイマーを進める
//
// 備考:
//   補間や速度計算は行わない（DroneMovementSystem が担う）。
//   BT を使わないのは、状態が固定で木構造にする利点が無く、ノード評価ごとの
//   GetComponent<> で 1000機×毎フレームの連続アクセスが崩れるため。ボス1体のように
//   判断が複雑で調整したい場合とは、コストの傾向が逆になる。
//   実行は L02_PreUpdate。陣形側(L02_Update)が今フレームの currentState を見て
//   形を作れるようにする。ステージを動かすと予備動作→突撃が1フレームずれる。
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Game/Logics/AI/Drone/DroneComponent.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/Time/TimeState.h"
#include <SimpleMath.h>

class DroneBrainSystem : public CCL::ECS::IfSystem<DroneBrainSystem,
                                                   CCL::ECS::Read<TransformComponent>,
                                                   CCL::ECS::Write<DroneComponent>,
                                                   CCL::ECS::Read<TimeStateComponent>>
{
public:
    DroneBrainSystem() : IfSystem("DroneBrainSystem") {}

    virtual ~DroneBrainSystem() override = default;

    virtual void Update(float dt) override;

private:
    // 陣形スロットへの初回接近がまだ必要か。距離だけで判定すると Idle の追従で
    // 閾値をまたいで MoveToTarget が点滅するので、hasReachedSlot にラッチする。
    bool NeedsApproach(const TransformComponent &trans, DroneComponent &drone) const;

    // 吹き飛ばし・立て直しのタイマーを進める。true なら硬直中（通常AIを行わない）。
    bool UpdateStun(DroneComponent &drone, float dt) const;

    // 突撃を切り上げるべきか（個別寿命の攻撃のみ）。主に飛距離で判定し、経過時間は
    // 壁際・遠距離で飛距離が伸びない場合の保険にする。
    bool HasFinishedCharge(const TransformComponent &trans, DroneComponent &drone, float dt) const;

    // FireCharge へ入った瞬間だけ行う初期化。ここで方向を固定するので、移動層の
    // 「タメ→リリース」は方向を変えない直進になる（ホーミングしない）。
    void BeginCharge(DroneComponent                     &drone,
                     const DirectX::SimpleMath::Vector3 &playerPos,
                     bool                                hasPlayer) const;

    static constexpr float kArriveThreshold = 2.5f;  // 陣形スロットへ到着とみなす距離 [m]
    static constexpr float kChargeWindup    = 0.4f;  // 突撃前のタメ時間 [秒]
    static constexpr float kRecoverBase     = 0.30f; // 吹き飛ばし後に通常AIへ戻るまで [秒]
    static constexpr float kRecoverStagger  = 0.08f; // 立て直しの機体差。一斉だと群れが機械的に見える

    // この距離 [m] を飛んだら突撃を終える。短いと届く前にやめるので、間合いから逆算する。
    //   発射地点はボスの 14m 上、BTの発動条件は 距離>30m なので、水平 50m なら
    //   √(50²+14²) ≒ 52m の飛距離が要る。56m あれば水平 54m まで貫ける。
    static constexpr float kChargeRange = 56.0f;

    static constexpr float kChargeTimeout = 2.0f; // 突撃の最長時間 [秒]（壁際用の保険）
    static constexpr float kBreakawayTime = 1.6f; // 突き抜けた後に減速する時間 [秒]
};
