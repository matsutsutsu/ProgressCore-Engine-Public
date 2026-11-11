#pragma once
// ===========================================================================
// 概要: チュートリアル中、プレイヤーとボスの HP を下限で止める System
//
// 責務:
//   - プレイヤーの HP を kPlayerMinHealth より下げない（倒れない）
//   - ボスの HP を、第2形態へ移るしきい値より下げない（形態移行が始まらない）
//
// 備考: チュートリアルは勝ち負けの場ではないので、倒れたり形態移行が始まったりして練習が途切れないようにする
//       無敵にはしないので、のけぞりと HP の減りは本番と同じに見える
//       DamageResolutionSystem（L06_DamageApply）と HealthSystem（L06_HealthManagement）の間で走る
//       TutorialScene だけが登録する
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Game/Logics/Combat/Components/HealthComponent.h"

class TutorialHealthGuardSystem : public CCL::ECS::IfSystem<
    TutorialHealthGuardSystem,
    CCL::ECS::Write<HealthComponent>>
{
public:
    TutorialHealthGuardSystem() : IfSystem("TutorialHealthGuardSystem") {}

    void Update(float dt) override;

    // プレイヤーとボスを ForEach の引数に取らずに見分けるので、ここで申告する
    std::vector<CCL::ECS::TypeID> GetReadTypes() const override;
};
