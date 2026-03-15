#pragma once
#include "CoinComponent.h"
#include "ECS/System/CCL_System.h"
#include "Engine/GamePlay/Physics/RigidBody/RigidBodyComponent.h"
#include "Engine/GamePlay/Transform/TransformComponent.h"
#include <DirectXMath.h>

// CoinComponent と TransformComponent を持つエンティティを自動処理するシステム
class CoinSystem : public CCL::ECS::IfSystem<CoinSystem,
                       CCL::ECS::Write<CoinComponent>,
                       CCL::ECS::Write<TransformComponent>,
                       CCL::ECS::Write<RigidBodyComponent>> {
  public:
    CoinSystem();
    virtual ~CoinSystem() = default;

    void Initialize() override;

    void Update(float dt) override;

  private:
    // 地面判定の高さ（PhysicsSystemの床補正と合わせる）
    const float GROUND_LEVEL = 0.1f;

    void  SpawnCoin(int amount, const DirectX::XMFLOAT3 &spawnPos); 

};