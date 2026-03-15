#pragma once
#include "ECS/System/CCL_System.h"
#include "Engine/GamePlay/Transform/TransformComponent.h"
#include "Game/Logics/Character/Enemy/EnemyComponent.h"
#include "Game/Logics/Combat/DeadTag.h"

// 【反応】敵の死亡時セットアップ（ドロップ＆ディゾルブ開始）を行うシステム
class EnemyDeathSystem : public CCL::ECS::IfSystem < EnemyDeathSystem,
                             CCL::ECS::Read<EnemyComponent>,
                             CCL::ECS::Read<Tag::DeadTag>,
                             CCL::ECS::Read<TransformComponent>> {
  public:
    EnemyDeathSystem() : IfSystem("EnemyDeathSystem") {}
    void Update(float dt) override;
};