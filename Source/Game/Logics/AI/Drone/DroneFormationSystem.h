// IFormation 方式の陣形計算システム（switch廃止版）。
//        各ドローンの currentFormation を見て registry から陣形を引き、
//        targetPosition を計算する。移動は既存 DroneMovementSystem が担当。
#pragma once
#include <vector>

#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/ECS/Common/CCL_TypeInfo.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Game/Logics/AI/Drone/DroneComponent.h"
#include "Game/Logics/AI/Drone/Formation/IFormation.h"
#include "Game/Logics/Character/Boss/Components/BossCommandComponent.h"

// ドローンの目標座標を陣形に応じて計算するシステム。
class DroneFormationSystem : public CCL::ECS::IfSystem<DroneFormationSystem,
                                 CCL::ECS::Write<DroneComponent>>
{
    using Base = CCL::ECS::IfSystem<DroneFormationSystem, CCL::ECS::Write<DroneComponent>>;

  public:
    DroneFormationSystem() : IfSystem("DroneFormationSystem"){}

    void Initialize() override;

    // 実行順序の宣言（TaskScheduler が同一ステージ内の順序決定に使う）。
    //        DroneCommand が drone.currentFormation を書き、こちらがそれを読んでスロットを計算する。
    std::vector<const char *> GetRunAfter() const override
    {
        return {"BehaviorTreeSystem", "DroneCommandSystem", "SquadUpdateSystem"};
    }

    // MakeContext がボスの Transform と BossCommandComponent を View で引くので申告する。
    //   テンプレート引数には足せない（ForEach の絞り込みまで変わってしまうため）。
    std::vector<CCL::ECS::TypeID> GetReadTypes() const override
    {
        auto types = Base::GetReadTypes();
        types.push_back(CCL::ECS::TypeInfo<BossCommandComponent>::ID());
        types.push_back(CCL::ECS::TypeInfo<TransformComponent>::ID());
        return types;
    }

    void Update(float dt) override;

  private:
    // ボス位置・プレイヤー位置など「場の情報」を組み立てる。
    CCL::AI::Formation::FormationContext MakeContext(float dt) const;

    float m_elapsed = 0.0f; // 回転アニメ用の経過時間
};
