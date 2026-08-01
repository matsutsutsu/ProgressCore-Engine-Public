#pragma once
/// @file CharacterAnimSyncSystem.h
/// @brief 物理(Jolt)や入力状態をアニメーションの「掲示板(AnimParameters)」に自動翻訳・転記するシステム。
///        フットスライディング（足滑り）防止の動的再生速度スケーリングも担当する。

#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Physics/Components/Character/JoltCharacterHandleComponent.h"
#include "Engine/Physics/Components/Character/JoltCharacterConfigComponent.h"
#include "Engine/Animation/Components/AnimParametersComponent.h"
#include "Engine/Animation/Components/AnimatorComponent.h"

// ※「ダッシュ中か」のようなゲーム固有の状態は、ゲーム側の
//   PlayerAnimParamSyncSystem が AnimParameters へ直接 push する。
//   本システムは物理(Jolt)由来の値だけを担当する。

class CharacterAnimSyncSystem : public CCL::ECS::IfSystem<
                                    CharacterAnimSyncSystem,
                                    CCL::ECS::Read<JoltCharacterHandleComponent>,
                                    CCL::ECS::Read<JoltCharacterConfigComponent>,
                                    CCL::ECS::Write<AnimParametersComponent>,
                                    CCL::ECS::Write<AnimatorComponent>>
{
  public:
    CharacterAnimSyncSystem()
        : IfSystem("CharacterAnimSyncSystem")
    {
    }
    virtual ~CharacterAnimSyncSystem() = default;

    void Update(float dt) override;
};