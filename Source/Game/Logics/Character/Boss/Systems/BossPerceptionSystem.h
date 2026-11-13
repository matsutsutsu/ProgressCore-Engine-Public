#pragma once
// ===========================================================================
// 概要: BattleContext と各ボスの状態から、BT の判断に使う値を blackboard へ書く System
//
// 責務:
//   - 狙う相手までの距離・角度・近づく速さ・横へ回る速さを書く
//   - HP の割合と、蓄積した被弾の割合を書く
//   - ステージの端までの距離と、左右に進める距離（C_SpaceOnLeft / Right の材料）を書く
//   - DamageTakenEvent を受けて被弾量を足し、被弾からの経過時間を 0 に戻す
//
// 備考: ここが blackboard に書くので、BT は HealthComponent もプレイヤーの探し方も知らずに済む
//       被弾の蓄積は、足すのがここ、読むのが C_DamageThresholdExceeded、0 に戻すのが A_ResetDamage
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Game/Logics/AI/BehaviorTree/Data/BehaviorTreeComponents.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Game/Logics/Combat/Components/HealthComponent.h"
#include "Engine/Time/Components/TimeState.h"
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

    void Initialize() override;
    void Update(float dt) override;

private:
    // 自分の正面から見た相手の角度（度）。正が右、負が左、±180 が真後ろ
    // 真上から見た角度だけを測る。見上げ角は間合いの判断に使わない
    float SignedAngleToTargetDeg(const TransformComponent           &trans,
                                 const DirectX::SimpleMath::Vector3 &targetPos) const;

    // 相手が自分へ近づく速さ（m/秒）。正が接近、負が後退
    // 相手の速度を「相手→自分」の向きへ射影するだけで、自分の速度は含めない
    float TargetClosingSpeed(const DirectX::SimpleMath::Vector3 &myPos,
                             const DirectX::SimpleMath::Vector3 &targetPos,
                             const DirectX::SimpleMath::Vector3 &targetVelocity) const;

    // 相手が自分の周りを横へ回る速さ（m/秒、0 以上）。左右は問わない
    // 相手の速度から「相手→自分」の向きの成分を除いた大きさで、自分の速度は含めない
    float TargetLateralSpeed(const DirectX::SimpleMath::Vector3 &myPos,
                             const DirectX::SimpleMath::Vector3 &targetPos,
                             const DirectX::SimpleMath::Vector3 &targetVelocity) const;

    // ステージの端までの距離（m）。正が内側。真上から見た距離だけを測る
    float DistanceToArenaEdge(const DirectX::SimpleMath::Vector3 &myWorldPos,
                              const BattleContext                &battle) const;

    // dir（水平・長さ1）へまっすぐ進んで、ステージの端に着くまでの距離（m）
    float SpaceToArenaEdge(const DirectX::SimpleMath::Vector3 &myWorldPos,
                           const DirectX::SimpleMath::Vector3 &dir,
                           const BattleContext                &battle) const;

    // 自分から見た「中央の方向」と「相手の方向」のなす角（度）
    // 0=相手が中央側を塞いでいる
    float TargetAngleFromCenterDeg(const DirectX::SimpleMath::Vector3 &myWorldPos,
                                   const DirectX::SimpleMath::Vector3 &targetPos,
                                   const BattleContext                &battle) const;
};
