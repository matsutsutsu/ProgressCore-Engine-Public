// 突撃中のドローンの当たり判定を開け閉めするシステム。危険な区間だけ Hitbox を
// 有効にし、接触の報告そのものは HitboxCollisionSystem が受ける。
#pragma once
#include <vector>

#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/ECS/Common/CCL_TypeInfo.h"
#include "Game/Logics/AI/Drone/DroneComponent.h"
#include "Game/Logics/Combat/CombatComponents.h"

class DroneHitSystem : public CCL::ECS::IfSystem<DroneHitSystem,
                           CCL::ECS::Read<DroneComponent>,
                           CCL::ECS::Write<HitboxComponent>>
{
public:
    DroneHitSystem() : IfSystem("DroneHitSystem") {}

    // 突撃の状態が確定した後でなければ1フレームずれる。
    //   DroneBrainSystem は L02_PreUpdate なのでステージの壁で既に手前と決まっている。
    //   ここに書くと「別ステージを指している」とスケジューラに警告されるだけなので、
    //   同じ L03_PrePhysics の DroneMovementSystem だけを宣言する。
    std::vector<const char *> GetRunAfter() const override
    {
        return {"DroneMovementSystem"};
    }

    void Update(float dt) override;

    /// 判定が開いている機数。当たらないときに「判定が閉じている」のか
    /// 「形状が届いていない」のかを切り分けるために出す。
    void OnGui() override;

  private:
    int m_droneCount   = 0;
    int m_activeCount  = 0;
    int m_stateCount[5] = {0, 0, 0, 0, 0};
};
