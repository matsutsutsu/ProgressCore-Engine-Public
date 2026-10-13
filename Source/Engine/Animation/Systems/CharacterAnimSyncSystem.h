#pragma once
// ===========================================================================
// 概要: キャラクターの物理状態をアニメーションパラメータへ書き込む System（L02_PreUpdate）。
//
// 責務:
//   - Jolt の CharacterVirtual から Speed / YVelocity / IsGrounded を書き、OnJump / OnLand トリガーを立てる
//   - 移動速度に合わせてロコモーションの再生速度を伸縮させ、足滑りを防ぐ（footSlideMultiplier）
//
// 備考: 扱うのは物理由来の値だけ。「ダッシュ中か」のようなゲーム固有の状態は
//       ゲーム側の PlayerAnimParamSyncSystem が書く。
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Physics/Components/Character/JoltCharacterHandleComponent.h"
#include "Engine/Physics/Components/Character/JoltCharacterConfigComponent.h"
#include "Engine/Animation/Components/AnimParametersComponent.h"
#include "Engine/Animation/Components/AnimatorComponent.h"

class CharacterAnimSyncSystem : public CCL::ECS::IfSystem<
                                    CharacterAnimSyncSystem,
                                    CCL::ECS::Read<JoltCharacterHandleComponent>,
                                    CCL::ECS::Read<JoltCharacterConfigComponent>,
                                    CCL::ECS::Write<AnimParametersComponent>,
                                    CCL::ECS::Write<AnimatorComponent>>
{
public:
    CharacterAnimSyncSystem() : IfSystem("CharacterAnimSyncSystem") {}
    virtual ~CharacterAnimSyncSystem() = default;

    void Update(float dt) override;
};