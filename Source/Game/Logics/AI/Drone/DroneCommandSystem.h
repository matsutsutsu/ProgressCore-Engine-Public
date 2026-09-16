// ボスの命令（BossCommandComponent）を全ドローンへ配布する「伝令」システム。
// ボスBT は自分の出力にだけ書き込み、1000機のドローンを直接触らない。
//
//   requestFormation  → 各 DroneComponent.currentFormation
//   droneAttackActive → 各 DroneComponent.wantsAttack
#pragma once
#include <vector>
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/ECS/Common/CCL_TypeInfo.h"
#include "Game/Logics/AI/Drone/DroneComponent.h"
#include "Game/Logics/Character/Boss/Components/BossCommandComponent.h"

class DroneCommandSystem : public CCL::ECS::IfSystem<DroneCommandSystem,
                                                     CCL::ECS::Write<DroneComponent>>
{
    using Base = CCL::ECS::IfSystem<DroneCommandSystem, CCL::ECS::Write<DroneComponent>>;

public:
    DroneCommandSystem()
        : IfSystem("DroneCommandSystem")
    {
    }

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
