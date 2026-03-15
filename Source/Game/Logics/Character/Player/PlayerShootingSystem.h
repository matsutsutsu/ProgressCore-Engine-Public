#pragma once
#include "ECS/System/CCL_System.h"

// テンプレート引数として必要なコンポーネント定義
#include "Engine/GamePlay/Animation/AnimatorComponent.h"
#include "Engine/GamePlay/Graphics/Core/ModelComponent.h"
#include "Engine/GamePlay/Transform/TransformComponent.h"
#include "Game/Logics/Character/Player/PlayerComponent.h"
#include "Game/Logics/Combat/Weapon/Core/GunComponent.h"



// プレイヤー、位置、銃の3つを持つエンティティを処理
class PlayerShootingSystem : public CCL::ECS::IfSystem<PlayerShootingSystem,
                                 CCL::ECS::Read<PlayerComponent>,
                                 CCL::ECS::Read<TransformComponent>,
                                 CCL::ECS::Write<GunComponent>,
                                 CCL::ECS::Write<AnimatorComponent>,
                                 CCL::ECS::Read<ModelComponent>> {
  private:
    int _animIdAttack = -1; // キャッシュ用

  public:
    PlayerShootingSystem();
    virtual ~PlayerShootingSystem() = default;

    void Update(float dt) override;

  private:
    void Fire(GunComponent &gunComp, const TransformComponent &shooterTrans);
};