#pragma once

#include "ECS/System/CCL_System.h" // IfSystemの継承元

// IfSystemのテンプレート引数で使用するため、定義が必要なコンポーネント
#include "Engine/GamePlay/Animation/AnimatorComponent.h"
#include "Engine/GamePlay/Graphics/Core/ModelComponent.h"
#include "Engine/GamePlay/Transform/TransformComponent.h"
#include "Game/Logics/Character/Player/PlayerComponent.h"

#include "Engine/GamePlay/Physics/Character/JoltCharacterHandleComponent.h"
#include "Engine/GamePlay/Physics/Character/JoltCharacterConfigComponent.h"

class PlayerMoveSystem : public CCL::ECS::IfSystem<PlayerMoveSystem,
        CCL::ECS::Write<TransformComponent>,
        CCL::ECS::Read<JoltCharacterHandleComponent>,
        CCL::ECS::Read<JoltCharacterConfigComponent>,
        CCL::ECS::Read<PlayerComponent>,
        CCL::ECS::Write<AnimatorComponent>,
        CCL::ECS::Read<ModelComponent>> {
  public:
    PlayerMoveSystem();
    virtual ~PlayerMoveSystem() = default;

    void Update(float dt) override;

  private:
    // アニメーションIDをキャッシュする変数
    int _animIdIdle   = -1;
    int _animIdRun    = -1;
    int _animIdAttack = -1; // 攻撃ID
};