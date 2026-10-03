#pragma once
// ===========================================================================
// 概要: 突撃中のドローンの当たり判定（Hitbox）を開け閉めする System。
//
// 責務:
//   - 危険な区間（突撃のリリース中）だけ HitboxComponent.isActive を立てる
//   - 陣形ごとの当たり方（ダメージ・ノックバック・ヒットストップ）を Hitbox へ写す
//   - ダメージの帰属先をボスにする
//
// 備考: 接触そのものは Jolt のセンサーが拾い、HitboxCollisionSystem（L05）が
//       Hitbox×Hurtbox を見て DamageEvent を積む。ここは開け閉めだけを持つ。
// ===========================================================================
#include <vector>

#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/ECS/Common/CCL_TypeInfo.h"
#include "Game/Logics/AI/Drone/Components/DroneComponent.h"
#include "Game/Logics/Combat/CombatComponents.h"

class DroneHitSystem : public CCL::ECS::IfSystem<DroneHitSystem,
                                                 CCL::ECS::Read<DroneComponent>,
                                                 CCL::ECS::Write<HitboxComponent>>
{
public:
    DroneHitSystem() : IfSystem("DroneHitSystem") {}

    /**
     * @brief 同じステージで先に走らせる System を宣言する。
     *
     * 突撃の状態が確定した後でなければ1フレームずれる。
     *
     * DroneBrainSystem は L02_PreUpdate なので、ステージの壁で既に手前と決まっている。
     * ここに書くと「別ステージを指している」とスケジューラに警告されるだけなので、同じ L03_PrePhysics の DroneMovementSystem だけを宣言する。
     */
    std::vector<const char *> GetRunAfter() const override
    {
        return {"DroneMovementSystem"};
    }

    void Update(float dt) override;

    /**
     * @brief 判定が開いている機数を出す。
     *
     * 当たらないときに「判定が閉じている」のか「形状が届いていない」のかを切り分けるため。
     */
    void OnGui() override;

private:
    int m_droneCount    = 0;
    int m_activeCount   = 0;
    int m_stateCount[5] = {0, 0, 0, 0, 0};
};
