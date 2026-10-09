#pragma once
// ===========================================================================
// 概要: ドローン個体の判断層。DroneComponent.currentState を毎フレーム決める System。
//
// 責務:
//   - 硬直・突撃・離脱・接近・待機のうち、優先度順に最初に成立したものを選ぶ
//   - 突撃の開始を技へ伝え、個別寿命の突撃の終了判断を行う
//   - 吹き飛ばし・立て直しのタイマーを進める
//
// 備考:
//   補間や速度計算は行わない（DroneMovementSystem が担う）。
//
//   BT を使わないのは、状態が固定で木構造にする利点が無く、ノード評価ごとの
//   GetComponent<> で 1000機×毎フレームの連続アクセスが崩れるため。ボス1体のように
//   判断が複雑で調整したい場合とは、コストの傾向が逆になる。
//
//   実行は L02_PreUpdate。陣形側(L02_Update)が今フレームの currentState を見て
//   形を作れるようにする。ステージを動かすと予備動作→突撃が1フレームずれる。
//
//   調整値（到着の距離・立て直しや離脱の時間）は、.cpp の先頭にまとめてある。
//   突撃の始め方（タメ・撃つ方向）といつ切り上げるかは技ごとに違うので、技の側
//   （IFormation::OnChargeBegin / IsChargeOver）が持つ。
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Game/Logics/AI/Drone/Components/DroneComponent.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/Time/Components/TimeState.h"
#include <SimpleMath.h>

class DroneBrainSystem : public CCL::ECS::IfSystem<DroneBrainSystem,
                                                   CCL::ECS::Read<TransformComponent>,
                                                   CCL::ECS::Write<DroneComponent>,
                                                   CCL::ECS::Read<TimeStateComponent>>
{
public:
    DroneBrainSystem() : IfSystem("DroneBrainSystem") {}

    void Update(float dt) override;

private:
    /**
     * @brief 陣形スロットへの初回接近がまだ必要かを返す。
     *
     * 距離だけで判定すると、Idle の追従で閾値をまたいで MoveToTarget が点滅する。
     * そこで初回の到着を hasReachedSlot にラッチする。
     *
     * @param trans 機体の Transform。
     * @param drone 判定する機体。到着したら hasReachedSlot を true にする。
     * @return まだスロットへ向かう必要があれば true。
     */
    bool NeedsApproach(const TransformComponent &trans, DroneComponent &drone) const;

    // 吹き飛ばし・立て直しのタイマーを進める。true なら硬直中（通常AIを行わない）。
    bool UpdateStun(DroneComponent &drone, float dt) const;

    /**
     * @brief 突撃を切り上げるべきかを返す（個別寿命の攻撃のみ）。
     *
     * ここでは飛び出してからの飛距離と経過時間を測るだけで、切り上げるかは技（IFormation::IsChargeOver）に聞く。
     *
     * @param trans 機体の Transform。
     * @param drone 判定する機体。飛び出す前は飛距離の起点（chargeStartPos）と経過時間を置き直す。
     * @param dt    経過時間（秒）。
     * @return 技が「切り上げる」と答えたら true。
     */
    bool HasFinishedCharge(const TransformComponent &trans, DroneComponent &drone, float dt) const;
};
