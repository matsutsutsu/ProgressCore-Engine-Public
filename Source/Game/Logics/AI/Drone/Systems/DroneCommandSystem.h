#pragma once
// ===========================================================================
// 概要: ボスの命令（BossCommandComponent）を全ドローンへ配布する System。
//
// 責務:
//   - requestFormation を各 DroneComponent.currentFormation へ写す
//   - droneAttackActive を各 DroneComponent.wantsAttack へ写す
//   - 個別寿命の陣形では、信号の立ち上がりで attackPhase を Charging にする
//
// 備考: ボスBT は自分の BossCommandComponent にだけ書き込み、1000機のドローンを
//       直接触らない。配布はこの System に集める。
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

    // BossCommandComponent はテンプレート引数に足せない。Read<>/Write<> は ForEach の
    // 絞り込み条件も兼ねるので、足すとドローンが1機も回らなくなる。申告だけを足す。
    std::vector<CCL::ECS::TypeID> GetWriteTypes() const override
    {
        auto types = Base::GetWriteTypes();
        types.push_back(CCL::ECS::TypeInfo<BossCommandComponent>::ID());
        return types;
    }

    // ボスBTが今フレーム書いた命令を読む。BTより後に走らないと1フレーム古い命令を配る。
    std::vector<const char *> GetRunAfter() const override { return {"BehaviorTreeSystem"}; }

    void Update(float dt) override;
};
