// ===================================================================================
// ファイル: AnimSetupSystem.cpp
// ===================================================================================
#include "AnimSetupSystem.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

using namespace CCL::ECS;

void AnimSetupSystem::Update(float dt)
{
    // コンポーネントの追加（アーキタイプの移動）をループ内で行うと
    // イテレータが破壊されるため、対象となるエンティティIDを一度キャッシュする
    std::vector<EntityID> needsParams;
    std::vector<EntityID> needsStateMachine;

    // 1. 足りないコンポーネントを【個別に】調査する
    ForEachWithID([&](EntityID id, const AnimatorComponent &animator) {
        if (!_world->HasComponent<AnimParametersComponent>(id))
        {
            needsParams.push_back(id);
        }
        if (!_world->HasComponent<AnimStateMachineComponent>(id))
        {
            needsStateMachine.push_back(id);
        }
    });

    // 2. 足りないものだけを【個別に】付与する（ループ外での安全な構造変更）
    for (auto id : needsParams)
    {
        _world->AddComponent<AnimParametersComponent>(id);
    }
    for (auto id : needsStateMachine)
    {
        _world->AddComponent<AnimStateMachineComponent>(id);
    }
}

// 実行順序：AnimControllerSystem などのアニメーションロジックが走る前に
// 依存コンポーネントを確定させるため、L02_Update ステージに登録する。
REGISTER_LOGIC_SYSTEM(AnimSetupSystem, Priority::LogicStage::L02_Update);