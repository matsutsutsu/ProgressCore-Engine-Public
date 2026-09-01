#pragma once
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include <vector>

class JoltSandboxGuiSystem : public CCL::ECS::IfSystem<JoltSandboxGuiSystem,
    CCL::ECS::Write<TransformComponent>>
{
  private:
    // GUI入力用のパラメータ状態
    int   m_spawnCount   = 500;
    float m_spawnHeight  = 100.0f;
    float m_spreadRadius = 30.0f;

    int   m_towerFloors = 10;
    float m_meteorSize  = 10.0f;

    // スポーンしたエンティティのIDを記録するリスト（後で一括削除するため）
    std::vector<CCL::ECS::EntityID> m_sandboxEntities;

  public:
    JoltSandboxGuiSystem()
        : IfSystem("JoltSandboxGuiSystem")
    {
    }
    virtual ~JoltSandboxGuiSystem() = default;

    // 毎フレームの処理は持たない。GUI 操作をトリガーにした生成しか行わないため。
    void Update(float dt) override
    {
    }

    // GUI描画とイベントハンドリング
    void OnGui() override;

  private:
    // 多機能なスポーン用ヘルパー関数群
    void SpawnSingleBox(float x, float y, float z, bool isDynamic);
    void SpawnAvalanche();
    void SpawnBlockTower();
    void SpawnMeteorStrike();
    void ClearAllSandboxEntities();
};