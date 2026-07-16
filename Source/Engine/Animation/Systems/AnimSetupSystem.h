#pragma once
// ===================================================================================
// ファイル: AnimSetupSystem.h
// 概要: アニメーション関連の必須コンポーネントを自動付与するセットアップシステム
//
// [ 役割 ]
// AnimatorComponent を持つが、AnimParametersComponent / AnimStateMachineComponent /
// RootMotionComponent を持たないエンティティを検知し、不足分を自動で補完する。
// これにより、ユーザーは AnimatorComponent をアタッチするだけでアニメーション機能一式を
// 安全に使い始めることができ、コンポーネントの付け忘れバグを防止する（プレイヤー/ボス問わず）。
//
// ★ SetExclude は使わない：SetExclude<A, B, C>() は「A/B/Cのどれか1つでも持っていたら
//   除外」というOR判定のため、例えば AnimStateMachine だけを先にプレハブへ手動で
//   書いた（Muriel.jsonのようなケース）エンティティが AnimParameters/RootMotion を
//   永久に受け取れなくなる。毎フレーム HasComponent で個別に判定する方が
//   エンティティ数（キャラのみ、数体～数十体規模）に対して安全かつ十分安い。
// ===================================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Animation/Components/AnimatorComponent.h"
#include "Engine/Animation/Components/AnimParametersComponent.h"
#include "Engine/Animation/Components/RootMotionComponent.h"
#include "Engine/Animation/Core/AnimStateMachine.h"

class AnimSetupSystem : public CCL::ECS::IfSystem<AnimSetupSystem,
                            CCL::ECS::Read<AnimatorComponent>>
{
  public:
    AnimSetupSystem()
        : IfSystem("AnimSetupSystem")
    {
    }

    virtual ~AnimSetupSystem() = default;

    void Update(float dt) override;
};