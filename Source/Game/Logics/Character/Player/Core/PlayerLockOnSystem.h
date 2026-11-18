#pragma once
// ===========================================================================
// 概要: ロックオンの入力で、最も近い敵へのロックオンと解除を切り替える System
//
// 責務:
//   - ロックオン対象が死亡・破棄されたら解除する
//   - ボタンを押した瞬間に、解除するか、最も近い敵を選ぶ
//   - ロックオン状態を、このプレイヤーを映す TPS カメラへ毎フレーム書く
//     ラッシュ中は、ロックオンの代わりにラッシュの相手を書く（ロックオンなしでも同じカメラワークにする）
// ===========================================================================

#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Game/Logics/Character/Player/Core/PlayerComponent.h"
#include "Game/Logics/Character/Player/Core/PlayerInputComponent.h"
#include "Engine/Transform/Components/TransformComponent.h"

#include <vector>

class PlayerLockOnSystem : public CCL::ECS::IfSystem<
    PlayerLockOnSystem,
    CCL::ECS::Write<PlayerComponent>,
    CCL::ECS::Read<PlayerInputComponent>,
    CCL::ECS::Read<TransformComponent>>
{
public:
    PlayerLockOnSystem() : IfSystem("PlayerLockOnSystem") {}
    virtual ~PlayerLockOnSystem() = default;

    void Update(float dt) override;

    std::vector<CCL::ECS::TypeID> GetReadTypes() const override;
    std::vector<CCL::ECS::TypeID> GetWriteTypes() const override;

private:
    using Base = CCL::ECS::IfSystem<
        PlayerLockOnSystem,
        CCL::ECS::Write<PlayerComponent>,
        CCL::ECS::Read<PlayerInputComponent>,
        CCL::ECS::Read<TransformComponent>>;

    // カメラに狙わせる敵を返す
    // ラッシュ中で相手がいればラッシュの相手、それ以外はロックオンの相手
    CCL::ECS::EntityID PickCameraTarget(CCL::ECS::EntityID playerID, const PlayerComponent &player) const;
};
