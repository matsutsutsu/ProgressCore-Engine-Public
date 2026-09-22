// ボスの「状況認識」をBlackboardへ書き込む専用システム。ここが HP割合・距離・被弾を
// 担うことで、BT 側は HealthComponent もプレイヤー検索も知らずに済む。
//
// 被弾の蓄積は DamageTakenEvent の購読で積む（積む=ここ／読む=C_DamageThresholdExceeded
// ／戻す=A_ResetDamage）。イベント経由なので Combat 側は BT を知らないままでいられる。
#pragma once
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Game/Logics/AI/BehaviorTree/Data/BehaviorTreeComponents.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Game/Logics/Combat/HealthComponent.h"
#include "Engine/Time/TimeState.h"
#include <SimpleMath.h>

struct BattleContext;

/// BattleContext（世界の事実）から、各ボスの blackboard（主観の判断材料）を毎フレーム書く。
class BossPerceptionSystem : public CCL::ECS::IfSystem<
                                 BossPerceptionSystem,
                                 CCL::ECS::Write<BehaviorTreeComponent>,
                                 CCL::ECS::Read<TransformComponent>,
                                 CCL::ECS::Read<HealthComponent>,
                                 CCL::ECS::Read<TimeStateComponent>>
{
public:
    BossPerceptionSystem() : IfSystem("BossPerceptionSystem") {}

    /// DamageTakenEvent の購読を張る。被弾のたびに被弾量を積み、被弾からの経過時間を 0 に戻す。
    void Initialize() override;

    /// 距離・角度・HP割合・ステージ端までの距離などを blackboard へ書き直す。
    void Update(float dt) override;

private:
    /// 自分の正面から見た相手の角度 [度]。正=右、負=左、±180=真後ろ。
    /// 真上から見た角度だけを測る（見上げ角は間合いの判断に使わない）。
    float SignedAngleToTargetDeg(const TransformComponent           &trans,
                                 const DirectX::SimpleMath::Vector3 &targetPos) const;

    /// 相手が自分へ詰めてくる速さ [m/s]。正=接近、負=後退。
    /// 相手の速度を「相手→自分」の向きへ射影するだけ。自分の速度は入れない。
    float TargetClosingSpeed(const DirectX::SimpleMath::Vector3 &myPos,
                             const DirectX::SimpleMath::Vector3 &targetPos,
                             const DirectX::SimpleMath::Vector3 &targetVelocity) const;

    /// ステージの端まであと何 m か。正=内側。真上から見た距離だけを測る。
    float DistanceToArenaEdge(const DirectX::SimpleMath::Vector3 &myWorldPos,
                              const BattleContext                &battle) const;

    /// 自分から見た「中央の方向」と「相手の方向」のなす角 [度]。0=相手が中央側を塞いでいる。
    float TargetAngleFromCenterDeg(const DirectX::SimpleMath::Vector3 &myWorldPos,
                                   const DirectX::SimpleMath::Vector3 &targetPos,
                                   const BattleContext                &battle) const;
};
