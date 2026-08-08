#pragma once
/// @file PlayerMoveSystem.h
/// @brief 意思データを物理エンジン(Jolt CharacterVirtual)の速度情報へ変換し流し込むシステム。

#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/Time/TimeState.h"
#include "Game/Logics/Character/Player/Components/PlayerComponent.h"
#include "Engine/Physics/Components/Character/CharacterMovementInputComponent.h"
#include "Engine/Physics/Components/Character/JoltCharacterConfigComponent.h"
#include "Engine/Physics/Components/Character/JoltCharacterHandleComponent.h"
#include "Engine/Animation/Components/AnimParametersComponent.h"

class PlayerMoveSystem : public CCL::ECS::IfSystem<PlayerMoveSystem,
    CCL::ECS::Write<TransformComponent>, 
    CCL::ECS::Write<CharacterMovementInputComponent>,
    CCL::ECS::Read<JoltCharacterConfigComponent>,
    CCL::ECS::Read<JoltCharacterHandleComponent>,
    CCL::ECS::Write<PlayerComponent>,
    CCL::ECS::Write<AnimParametersComponent>, 
    CCL::ECS::Read<TimeStateComponent>>
{
public:
    PlayerMoveSystem() : IfSystem("PlayerMoveSystem") {}
    virtual ~PlayerMoveSystem() = default;

    void Update(float dt) override;

private:
    /// @brief コヨーテタイムと先行入力バッファを用いたジャンプ判定処理
    ///        ジャンプ成立時は worldPos を載せて PlayerJumpedEvent を発行する（その場VFX用）。
    void UpdateJumpLogic(float dt, bool isGrounded, CharacterMovementInputComponent &inputData, PlayerComponent &player, AnimParametersComponent &animParams, const DirectX::XMFLOAT3 &worldPos);

    /// @brief プロシージャル・バンキング（傾き）の計算と、物理/描画の回転分離処理
    void UpdateBankingAndRotation(float dt, const CharacterMovementInputComponent &inputData, float walkSpeed, PlayerComponent &player, TransformComponent &trans, JPH::CharacterVirtual *character);
};