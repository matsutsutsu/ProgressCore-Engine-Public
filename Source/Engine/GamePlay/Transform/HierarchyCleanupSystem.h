#pragma once

#include "Engine/GamePlay/Core/DestroyTag.h"
#include "Engine/GamePlay/Transform/TransformComponent.h"
#include "ECS/System/CCL_System.h"
#include <vector>


class HierarchyCleanupSystem : public CCL::ECS::IfSystem<
    HierarchyCleanupSystem, 
    CCL::ECS::Read<Tag::DestroyTag>> {
  public:
    HierarchyCleanupSystem();
    virtual ~HierarchyCleanupSystem() = default;

    // フレームの最後に実行され、タグ付きエンティティと子供を回収する
    void Update(float dt) override;

  private:
    // 再帰的に子供のIDをリストに追加するヘルパー関数
    void CollectChildrenRecursive(
        CCL::ECS::EntityID currentID, std::vector<CCL::ECS::EntityID> &outList);
};

