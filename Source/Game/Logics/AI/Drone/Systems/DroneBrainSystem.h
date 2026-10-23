#pragma once
// ===========================================================================
// 概要: ドローン個体の判断層。DroneComponent.currentState を毎フレーム決める System。
//
// 責務:
//   - 突撃・離脱・接近・待機のうち、優先度順に最初に成立したものを選ぶ
//   - 突撃の開始を技へ伝え、個別寿命の突撃の終了判断を行う
//
// 備考:
//   BT を使わないのは、状態が固定で木にする利点が無く、ノード評価ごとの GetComponent<> で
//   1000機ぶんの連続アクセスが崩れるため。
//
//   突撃の始め方と切り上げ方は技ごとに違うので、技の側（IFormation::OnChargeBegin / IsChargeOver）が持つ。
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Game/Logics/AI/Drone/Components/DroneComponent.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/Time/Components/TimeState.h"
#include <SimpleMath.h>

namespace CCL::AI::Formation { class FormationRegistry; }

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

    /**
     * @brief 突撃を切り上げるべきかを返す（個別寿命の攻撃のみ）。
     *
     * 飛び出してからの飛距離と経過時間を測り、切り上げるかは技（IFormation::IsChargeOver）に聞く。
     *
     * @param formations 技の一覧（World の Resource）。無ければ切り上げない。
     * @param trans      機体の Transform。
     * @param drone      判定する機体。飛び出す前は飛距離の起点（chargeStartPos）と経過時間を置き直す。
     * @param dt         経過時間（秒）。
     * @return 技が「切り上げる」と答えたら true。
     */
    bool HasFinishedCharge(const CCL::AI::Formation::FormationRegistry *formations,
                           const TransformComponent &trans, DroneComponent &drone, float dt) const;
};
