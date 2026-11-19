#pragma once
// ===========================================================================
// 概要: ドローン個体の判断層。DroneComponent.currentState を毎フレーム決める System
//
// 責務:
//   - 突撃・離脱・接近・待機のうち、優先度順に最初に成立したものを選ぶ
//   - 突撃の開始を技へ伝え、技が自分で終える突撃の終了判断を行う
//
// 備考:
//   BT を使わないのは、状態が固定で木にする利点がなく、ノード評価ごとの GetComponent<> で
//   1000機ぶんの連続アクセスが崩れるため
//
//   突撃の始め方と切り上げ方は技ごとに違うので、技の側（IFormation::OnChargeBegin / IsChargeOver）が持つ
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Game/Logics/AI/Drone/Components/DroneComponent.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/Time/Components/TimeState.h"
#include <SimpleMath.h>

namespace CCL::AI::Formation { class IFormation; }

class DroneBrainSystem : public CCL::ECS::IfSystem<
    DroneBrainSystem,
    CCL::ECS::Read<TransformComponent>,
    CCL::ECS::Write<DroneComponent>,
    CCL::ECS::Read<TimeStateComponent>>
{
public:
    DroneBrainSystem() : IfSystem("DroneBrainSystem") {}

    void Update(float dt) override;

private:
    // 陣形スロットへの初回の接近がまだ必要かを返す。到着したら hasReachedSlot を立てる
    // 距離だけで判定すると、Idle の追従で閾値をまたいで MoveToTarget が点滅するので、初回の到着をラッチする
    bool NeedsApproach(const TransformComponent &trans, DroneComponent &drone) const;

    // 突撃の飛距離の起点（chargeStartPos）と経過時間（chargeTimer）を記録する
    // 飛び出す前は起点を今の位置に置き直し続け、飛び出した後だけ時間を数える。formation が nullptr なら直進として扱う
    void TrackChargeFlight(const CCL::AI::Formation::IFormation *formation,
                           const TransformComponent &trans, DroneComponent &drone, float dt) const;

    // 技（IsChargeOver）に、飛距離と経過時間から突撃を切り上げるかを聞く。formation が nullptr なら切り上げない
    bool IsChargeFinished(const CCL::AI::Formation::IFormation *formation,
                          const TransformComponent &trans, const DroneComponent &drone) const;
};
