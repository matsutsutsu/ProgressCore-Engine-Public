#pragma once
// ===========================================================================
// 概要: ボスの命令（BossCommandComponent）を全ドローンへ配布する System。
//
// 責務:
//   - requestFormation を各 DroneComponent.currentFormation へ写す
//   - droneAttackActive の立ち上がりで各機の wantsAttack を立て、下りたら下ろす
//
// 備考: ボスBT は自分の BossCommandComponent にだけ書き、ドローンを直接触らない。
// ===========================================================================
#include <vector>
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/ECS/Common/CCL_TypeInfo.h"
#include "Game/Logics/AI/Drone/Components/DroneComponent.h"
#include "Game/Logics/Character/Boss/Components/BossCommandComponent.h"

class DroneCommandSystem : public CCL::ECS::IfSystem<DroneCommandSystem,
                                                     CCL::ECS::Write<DroneComponent>>
{
    using Base = CCL::ECS::IfSystem<DroneCommandSystem, CCL::ECS::Write<DroneComponent>>;

public:
    DroneCommandSystem() : IfSystem("DroneCommandSystem") {}

    /**
     * @brief テンプレート引数以外に書く型（BossCommandComponent）を申告する。
     *
     * テンプレート引数に足すと ForEach の絞り込み条件にもなり、ドローンが1機も回らなくなる。
     */
    std::vector<CCL::ECS::TypeID> GetWriteTypes() const override
    {
        auto types = Base::GetWriteTypes();
        types.push_back(CCL::ECS::TypeInfo<BossCommandComponent>::ID());
        return types;
    }

    // BT より後に走らないと、1フレーム古い命令を配る
    std::vector<const char *> GetRunAfter() const override { return {"BehaviorTreeSystem"}; }

    void Update(float dt) override;
};
