#pragma once
// ===================================================================================
// ファイル: AnimSetupSystem.h
// 概要: アニメーション関連の必須コンポーネントを自動付与するセットアップシステム
//
// [ 役割 ]
// AnimatorComponent を持つが、AnimParametersComponent または
// AnimStateMachineComponent を持たないエンティティを検知し、不足分を自動で補完する。
// これにより、ユーザーは AnimatorComponent をアタッチするだけでアニメーション機能一式を
// 安全に使い始めることができ、コンポーネントの付け忘れバグを防止する。
// ===================================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Animation/Components/AnimatorComponent.h"
#include "Engine/Animation/Components/AnimParametersComponent.h"
#include "Engine/Animation/Core/AnimStateMachine.h"

class AnimSetupSystem : public CCL::ECS::IfSystem<AnimSetupSystem,
                            CCL::ECS::Read<AnimatorComponent>>
{
  public:
    AnimSetupSystem()
        : IfSystem("AnimSetupSystem")
    {
        // 必須コンポーネントを既に両方とも持っているエンティティは処理対象から除外する
        SetExclude<AnimParametersComponent, AnimStateMachineComponent>();
    }

    virtual ~AnimSetupSystem() = default;

    void Update(float dt) override;
};