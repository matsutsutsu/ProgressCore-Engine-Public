#pragma once
// ===========================================================================
// 概要: プレイヤーの空中の判定とジャンプを処理する System
//
// 責務:
//   - 接地の状態から、猶予付きで isAirborne を決める
//   - 2段ジャンプを判定し、物理へジャンプを依頼する。アニメの Jump / DoubleJump トリガーもここで立てる
//
// 備考: 向きと傾きは PlayerFacingSystem
//       移動の速さは書かない（歩き・走りは PlayerMoveSystem、ダッシュは PlayerDashSystem）
// ===========================================================================

#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Time/Components/TimeState.h"
#include "Game/Logics/Character/Player/Core/PlayerComponent.h"
#include "Game/Logics/Character/Player/Core/PlayerInputComponent.h"
#include "Game/Logics/Character/Player/Rush/PlayerRushComponent.h"
#include "Engine/Physics/Components/Character/CharacterMovementComponent.h"
#include "Engine/Physics/Components/Character/JoltCharacterHandleComponent.h"
#include "Engine/Animation/Components/AnimParametersComponent.h"

#include <vector>

class PlayerJumpSystem : public CCL::ECS::IfSystem<
    PlayerJumpSystem,
    CCL::ECS::Write<CharacterMovementComponent>,
    CCL::ECS::Read<PlayerInputComponent>,
    CCL::ECS::Read<JoltCharacterHandleComponent>,
    CCL::ECS::Write<PlayerComponent>,
    CCL::ECS::Write<AnimParametersComponent>,
    CCL::ECS::Read<PlayerRushComponent>,
    CCL::ECS::Read<TimeStateComponent>>
{
public:
    PlayerJumpSystem() : IfSystem("PlayerJumpSystem") {}
    virtual ~PlayerJumpSystem() = default;

    void Update(float dt) override;

    // PlayerFacingSystem より先に走らせる。使うデータは重ならないが、
    // どちらも PlayerComponent を書くので、宣言しないと順が名前の並びまかせになる
    std::vector<const char *> GetRunBefore() const override { return {"PlayerFacingSystem"}; }

private:
    // 接地の状態を更新し、跳べるなら物理へジャンプを依頼する（2段ジャンプを含む）
    // worldPos は PlayerJumpedEvent に載せるプレイヤーの位置
    void UpdateJumpLogic(float dt, bool isGrounded, bool jumpPressed, CharacterMovementComponent &movement, PlayerComponent &player, AnimParametersComponent &animParams, const DirectX::XMFLOAT3 &worldPos);
};
