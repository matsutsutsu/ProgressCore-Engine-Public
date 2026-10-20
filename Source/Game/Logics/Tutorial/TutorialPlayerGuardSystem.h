#pragma once
// ===========================================================================
// 概要: チュートリアル中、プレイヤーの HP が 0 にならないよう下限で止める System。
//
// 責務:
//   - プレイヤーの HP を kMinHealth より下げない
//
// 備考: チュートリアルは勝ち負けを決める場ではないので、倒れて練習が途切れないようにする。
//       無敵にはしない。被弾ののけぞりと HP の減りは本番と同じに見せ、「当たると痛い」ことは伝える。
//       ダメージで HP を引く DamageResolutionSystem（L06_DamageApply）と、0 以下で DeadTag を付ける
//       HealthSystem（L06_HealthManagement）の間の段で走る。ここで戻しておけば、0 の HP を見る System は無い。
//       全体登録はせず、TutorialScene だけが登録する。
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Game/Logics/Combat/Components/HealthComponent.h"
#include "Game/Logics/Character/Player/Core/PlayerComponent.h"

class TutorialPlayerGuardSystem : public CCL::ECS::IfSystem<TutorialPlayerGuardSystem,
                                                            CCL::ECS::Write<HealthComponent>,
                                                            CCL::ECS::Read<PlayerComponent>>
{
public:
    TutorialPlayerGuardSystem() : IfSystem("TutorialPlayerGuardSystem") {}

    void Update(float dt) override;

private:
    static constexpr float kMinHealth = 1.0f; // HP の下限。0 より上なら倒れない
};
