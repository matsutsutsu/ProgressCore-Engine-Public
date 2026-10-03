#pragma once
// ===========================================================================
// 概要: BattleContext と各ボスの状態から、BT の判断に使う値を blackboard へ書く System。
//
// 責務:
//   - 狙う相手と、相手までの距離・角度・近づく速さを書く
//   - HP の割合と、蓄積した被弾の割合を書く
//   - ステージの端までの距離と、中央の方向に対する相手の角度を書く
//   - DamageTakenEvent を受けて被弾量を足し、被弾からの経過時間を 0 に戻す
//
// 備考: この System が HP・距離・被弾を blackboard に書くので、BT 側は HealthComponent も
//       プレイヤーの探し方も知らずに済む。
//       被弾の蓄積は、足す=ここ／読む=C_DamageThresholdExceeded／0 に戻す=A_ResetDamage。
//       イベントで受けるので、Combat 側は BT を知らない。
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Game/Logics/AI/BehaviorTree/Data/BehaviorTreeComponents.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Game/Logics/Combat/HealthComponent.h"
#include "Engine/Time/TimeState.h"
#include <SimpleMath.h>

struct BattleContext;

class BossPerceptionSystem : public CCL::ECS::IfSystem<
                                 BossPerceptionSystem,
                                 CCL::ECS::Write<BehaviorTreeComponent>,
                                 CCL::ECS::Read<TransformComponent>,
                                 CCL::ECS::Read<HealthComponent>,
                                 CCL::ECS::Read<TimeStateComponent>>
{
public:
    BossPerceptionSystem() : IfSystem("BossPerceptionSystem") {}

    // DamageTakenEvent を購読する。被弾のたびに被弾量を足し、被弾からの経過時間を 0 に戻す。
    void Initialize() override;

    // 距離・角度・HP の割合・ステージの端までの距離などを blackboard へ書き直す。
    void Update(float dt) override;

private:
    /**
     * @brief 自分の正面から見た相手の角度を返す。
     *
     * 真上から見た角度だけを測る（見上げ角は間合いの判断に使わない）。
     *
     * @param trans     自分の Transform。
     * @param targetPos 相手の位置（ワールド）。
     * @return 角度（度）。正=右、負=左、±180=真後ろ。
     */
    float SignedAngleToTargetDeg(const TransformComponent           &trans,
                                 const DirectX::SimpleMath::Vector3 &targetPos) const;

    /**
     * @brief 相手が自分へ近づく速さを返す。
     *
     * 相手の速度を「相手→自分」の向きへ射影するだけで、自分の速度は含めない。
     *
     * @param myPos          自分の位置（ワールド）。
     * @param targetPos      相手の位置（ワールド）。
     * @param targetVelocity 相手の速度（m/秒）。
     * @return 近づく速さ（m/秒）。正=接近、負=後退。
     */
    float TargetClosingSpeed(const DirectX::SimpleMath::Vector3 &myPos,
                             const DirectX::SimpleMath::Vector3 &targetPos,
                             const DirectX::SimpleMath::Vector3 &targetVelocity) const;

    // ステージの端までの距離（m）。正=内側。真上から見た距離だけを測る。
    float DistanceToArenaEdge(const DirectX::SimpleMath::Vector3 &myWorldPos,
                              const BattleContext                &battle) const;

    // 自分から見た「中央の方向」と「相手の方向」のなす角（度）。0=相手が中央側を塞いでいる。
    float TargetAngleFromCenterDeg(const DirectX::SimpleMath::Vector3 &myWorldPos,
                                   const DirectX::SimpleMath::Vector3 &targetPos,
                                   const BattleContext                &battle) const;
};
