#pragma once

#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/Physics/Components/Character/CharacterMovementInputComponent.h"
#include "Game/Logics/Character/Collision/CharacterWallComponents.h"
#include "Game/Logics/Character/Player/PlayerComponent.h"
#include "Game/Logics/Wand/Bullet/WandRuntimeComponents.h" // ★新しいヘッダに変更
#include "Game/Logics/Combat/DeadTag.h"

/**
 * @brief プレイヤーの「入力の意思」を「杖の発射意思」へ変換するブリッジシステム。
 */
class PlayerAttackSystem
    : public CCL::ECS::IfSystem<
    PlayerAttackSystem,
    CCL::ECS::Write<WandFireIntentComponent>,
    CCL::ECS::Read<CharacterMovementInputComponent>,
    CCL::ECS::Read<CharacterWallRadiusComponent>,
    CCL::ECS::Read<TransformComponent>,
    CCL::ECS::Read<PlayerComponent>>
{
public:
    PlayerAttackSystem() : IfSystem("PlayerAttackSystem")
    {
        // 死亡時は発射できないようにフィルタリング
        SetExclude<Tag::DeadTag>();
    }

    virtual ~PlayerAttackSystem() = default;

    void Update(float dt) override;
};