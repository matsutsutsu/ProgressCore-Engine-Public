#pragma once
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "BoidComponent.h"
#include <vector>

class BoidSystem : public CCL::ECS::IfSystem<BoidSystem,
                       CCL::ECS::Write<TransformComponent>,
                       CCL::ECS::Write<BoidComponent>>
{
  public:
    // GUIから調整可能な群れのパラメータ群（DOD最適化のためSystemで一元管理）
    struct BoidSettings
    {
        float maxSpeed       = 15.0f;
        float maxForce       = 8.0f; // 旋回力（ぬるっと動くか、キビキビ動くか）
        float neighborRadius = 5.0f; // 仲間を認識する半径

        // 3つの力のウェイト
        float separationWeight = 1.5f;
        float alignmentWeight  = 1.0f;
        float cohesionWeight   = 1.0f;

        // デモ領域の設定
        DirectX::SimpleMath::Vector3 boundsCenter = {0.0f, 20.0f, 0.0f};
        float                        boundsRadius = 30.0f;
        float                        boundsWeight = 2.0f;
    };

  private:
    BoidSettings m_settings;

    // マルチスレッドで安全に読み取るための「スナップショット」構造体
    struct BoidData
    {
        DirectX::SimpleMath::Vector3 position;
        DirectX::SimpleMath::Vector3 velocity;
    };

    std::vector<BoidData> m_snapshot;

  public:
    BoidSystem()
        : IfSystem("BoidSystem")
    {
        hasGui = true; // GUIを持つことを明示
    }
    virtual ~BoidSystem() = default;

    void Update(float dt) override;
    void OnGui() override;
};