#pragma once
// ===========================================================================
// 概要: ロックオンの入力で、最も近い敵へのロックオンと解除を切り替える System。
//
// 責務:
//   - ロックオン対象が死亡・破棄されたら解除する
//   - ボタンを押した瞬間に、ロックオン中なら解除し、そうでなければ最も近い EnemyTag を持つ敵を選ぶ
//   - ロックオン状態を、このプレイヤーを映す TPS カメラへ毎フレーム書く
// ===========================================================================

#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Game/Logics/Character/Player/Core/PlayerComponent.h"
#include "Game/Logics/Character/Player/Core/PlayerInputComponent.h"
#include "Engine/Transform/Components/TransformComponent.h"

class PlayerLockOnSystem : public CCL::ECS::IfSystem<PlayerLockOnSystem,
                                                     CCL::ECS::Write<PlayerComponent>,
                                                     CCL::ECS::Read<PlayerInputComponent>,
                                                     CCL::ECS::Read<TransformComponent>>
{
public:
    PlayerLockOnSystem() : IfSystem("PlayerLockOnSystem") {}
    virtual ~PlayerLockOnSystem() = default;

    void Update(float dt) override;
};
