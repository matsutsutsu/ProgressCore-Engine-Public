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


class BossPerceptionSystem : public CCL::ECS::IfSystem<
    BossPerceptionSystem,
    CCL::ECS::Write<BehaviorTreeComponent>,
    CCL::ECS::Read<TransformComponent>,
    CCL::ECS::Read<HealthComponent>,
    CCL::ECS::Read<TimeStateComponent>
>
{
public:
    BossPerceptionSystem() : IfSystem("BossPerceptionSystem") {}

    // DamageTakenEvent の購読を張る。
    void Initialize() override;

    void Update(float dt) override;
};
