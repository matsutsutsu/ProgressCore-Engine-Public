#pragma once
// 意思データを物理エンジン(Jolt CharacterVirtual)の速度情報へ変換し流し込むシステム。

#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/Time/TimeState.h"
#include "Game/Logics/Character/Player/Components/PlayerComponent.h"
#include "Game/Logics/Character/Player/Components/PlayerInputComponent.h"
#include "Engine/Physics/Components/Character/CharacterMovementComponent.h"
#include "Engine/Physics/Components/Character/JoltCharacterConfigComponent.h"
#include "Engine/Physics/Components/Character/JoltCharacterHandleComponent.h"
#include "Engine/Animation/Components/AnimParametersComponent.h"

class PlayerMoveSystem : public CCL::ECS::IfSystem<PlayerMoveSystem,
    CCL::ECS::Write<TransformComponent>, 
    CCL::ECS::Write<CharacterMovementComponent>,
    CCL::ECS::Read<PlayerInputComponent>,
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
    // 接地状態の更新と、ジャンプ成立の判定（2段ジャンプの可否）。
    // 押下の立ち上がりは PlayerInputComponent が解決済みなので、ここは
    // 「跳んでよいか」だけを判断し、結果を jumpImpulse として物理へ渡す。
    // 接地離れは airborneGraceTime の猶予付きで isAirborne へ反映する。
    // ジャンプ成立時は worldPos を載せて PlayerJumpedEvent を発行する（その場VFX用）。
    void UpdateJumpLogic(float dt, bool isGrounded, bool jumpPressed, CharacterMovementComponent &movement, PlayerComponent &player, AnimParametersComponent &animParams, const DirectX::XMFLOAT3 &worldPos);

    // プロシージャル・バンキング（傾き）の計算と、物理/描画の回転分離処理
    void UpdateBankingAndRotation(float dt, const CharacterMovementComponent &movement, float walkSpeed, PlayerComponent &player, TransformComponent &trans, JPH::CharacterVirtual *character);
};