#pragma once
// ===========================================================================
// 概要: AnimatorComponent を持つ Entity に、足りないアニメーション用コンポーネントを付ける System（L02_Update）。
//
// 責務:
//   - AnimParametersComponent / AnimStateMachineComponent / RootMotionComponent が無ければ付ける
//
// 備考:
//   - AnimatorComponent を付けるだけでアニメーション一式が動くようにし、付け忘れを防ぐ。
//   - SetExclude は使わない。SetExclude<A, B, C>() は「どれか 1 つでも持っていたら除外」なので、
//     例えば AnimStateMachine だけをプレハブに書いた Entity（Muriel.json など）が
//     AnimParameters / RootMotion を受け取れなくなる。対象はキャラだけで数も少ないので、
//     毎ステップ HasComponent で個別に調べても十分軽い。
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Animation/Components/AnimatorComponent.h"
#include "Engine/Animation/Components/AnimParametersComponent.h"
#include "Engine/Animation/Components/RootMotionComponent.h"
#include "Engine/Animation/Core/AnimStateMachine.h"

class AnimSetupSystem : public CCL::ECS::IfSystem<AnimSetupSystem,
                                                  CCL::ECS::Read<AnimatorComponent>>
{
public:
    AnimSetupSystem() : IfSystem("AnimSetupSystem") {}

    virtual ~AnimSetupSystem() = default;

    void Update(float dt) override;
};