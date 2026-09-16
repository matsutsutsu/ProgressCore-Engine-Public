#pragma once
// 切り払いのゲージ／ストックHUDを ECS UI で構築し、CleaveComponent の状態を毎フレーム連動させる。
//
// 設計方針:
//   - 「Initialize で UI エンティティ生成 → Update でデータ連動」の型（HUD共通パターン）。
//   - ゲージ(次のストックまでの蓄積)を UIProgressBar、ストック数を UIText に反映する。
//   - 表現(UI)と供給(CleaveComponent)を分離する。
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Game/Logics/Character/Player/Components/CleaveComponent.h"

class CleaveGaugeHudSystem : public CCL::ECS::IfSystem<CleaveGaugeHudSystem,
                                                       CCL::ECS::Read<CleaveComponent>>
{
public:
    CleaveGaugeHudSystem()
        : IfSystem("CleaveGaugeHudSystem")
    {
    }

    void Initialize() override;
    void Update(float dt) override;

private:
    CCL::ECS::EntityID m_gaugeID = CCL::ECS::InvalidEntityID;
    CCL::ECS::EntityID m_labelID = CCL::ECS::InvalidEntityID;
};
