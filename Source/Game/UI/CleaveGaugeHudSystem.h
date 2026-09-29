#pragma once
// ===========================================================================
// 概要: 画面左下に切り払いのゲージとストック数を作り、CleaveComponent に毎フレーム合わせる System。
//
// 責務:
//   - Initialize でゲージとラベルの UI エンティティを作る
//   - 次のストックまでのたまり具合をゲージに、ストック数をラベルに書く
//   - CleaveComponent を持つエンティティが居なければ隠す
// ===========================================================================
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
    CCL::ECS::EntityID m_gaugeID = CCL::ECS::InvalidEntityID; // ゲージ（UIProgressBar）
    CCL::ECS::EntityID m_labelID = CCL::ECS::InvalidEntityID; // ストック数のラベル（UIText）
};
