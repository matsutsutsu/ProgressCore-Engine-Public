#pragma once
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "OrbitBenchmarkComponent.h"
#include <cmath>

class ECSBenchmarkSystem : public CCL::ECS::IfSystem<ECSBenchmarkSystem,
                               CCL::ECS::Write<TransformComponent>,
                               CCL::ECS::Read<OrbitBenchmarkComponent>> // 自身は書き換えないのでRead権限！
{
  public:
    // GUIから調整可能な共通パラメータ群
    struct BenchmarkSettings
    {
        float orbitSpeed    = 1.0f;  // 全体の回転速度
        float waveSpeed     = 2.0f;  // 波打つ速度
        float waveAmplitude = 10.0f; // 波打つ高さ
        float radiusScale   = 1.0f;  // 全体の広がり具合
    };

  private:
    BenchmarkSettings m_settings;
    float             m_globalTime = 0.0f; // 全体の時計

  public:
    ECSBenchmarkSystem()
        : IfSystem("ECSBenchmarkSystem")
    {
        hasGui = true;
    }
    virtual ~ECSBenchmarkSystem() = default;

    void Update(float dt) override;
    void OnGui() override;
};