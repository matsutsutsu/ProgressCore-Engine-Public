#pragma once
// ===========================================================================
// 概要: パーティクルを作って確かめるためのシーン。ParticleScene のレベルを読み込む
//
// 責務:
//   - ParticleScene.level.json を読み込む
//   - 全 System を登録する（リザルト・ポーズは登録しない）
//
// 備考: エディタのシーン切り替えから開く
// ===========================================================================
#include "Engine/Scene/BaseScene.h"

class ParticleScene : public BaseScene
{
public:
    ParticleScene() = default;
    ~ParticleScene() override {};

    void Initialize() override;
    void Finalize() override;

    CCL::ECS::World         *GetWorld() override { return m_worldPtr; }
    CCL::ECS::SystemManager *GetSystemManager() override { return m_ecsContext->GetSystemManager(); }

protected:
    void RegisterSystems() override;
};