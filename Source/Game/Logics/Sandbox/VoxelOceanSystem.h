#pragma once
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "VoxelOceanComponent.h"
#include <cmath>

class VoxelOceanSystem : public CCL::ECS::IfSystem<VoxelOceanSystem,
                             CCL::ECS::Write<TransformComponent>,
                             CCL::ECS::Read<VoxelOceanComponent>> // Read権限に変更（自身は書き換えないため）
{
  public:
    // GUIから調整可能な波のパラメータ群
    struct OceanSettings
    {
        float waveSpeed     = 1.5f; // 全体的な波の進行速度
        float waveAmplitude = 1.0f; // 波の高さ（振幅）
        float waveFrequency = 1.0f; // 波の細かさ
        float heightOffset  = 0.0f; // 海面全体の基本の高さ
    };

  private:
    OceanSettings m_settings;
    float         m_globalTime = 0.0f; // 海全体の経過時間（Systemで一元管理）

  public:
    VoxelOceanSystem()
        : IfSystem("VoxelOceanSystem")
    {
    }
    virtual ~VoxelOceanSystem() = default;

    void Update(float dt) override;

    // エンジンのGUIループから呼ばれる仮想関数（※親クラスで定義されている前提）
    void OnGui() override;
};