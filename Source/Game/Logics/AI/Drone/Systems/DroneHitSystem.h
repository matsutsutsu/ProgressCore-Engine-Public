#pragma once
// ===========================================================================
// 概要: 突撃中のドローンの当たり判定（Hitbox）を開け閉めする System
//
// 責務:
//   - 技が当たる区間（IFormation::IsHitOpen）だけ HitboxComponent.isActive を立てる
//   - 開いた瞬間に、技の当たり方（ダメージ・ノックバック・ヒットストップ）を Hitbox へ写す
//   - ダメージの帰属先をボスにする
//
// 備考: 接触そのものは Jolt のセンサーが拾い、HitboxCollisionSystem（L05）が
//       Hitbox×Hurtbox を見て DamageEvent を積む。ここは開け閉めだけを持つ
// ===========================================================================
#include <vector>

#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/ECS/Common/CCL_TypeInfo.h"
#include "Game/Logics/AI/Drone/Components/DroneComponent.h"
#include "Game/Logics/Combat/Components/CombatComponents.h"

namespace CCL::AI::Formation { struct FormationCombat; }

class DroneHitSystem : public CCL::ECS::IfSystem<
    DroneHitSystem,
    CCL::ECS::Read<DroneComponent>,
    CCL::ECS::Write<HitboxComponent>>
{
public:
    DroneHitSystem() : IfSystem("DroneHitSystem") {}

    /**
     * @brief 同じステージで先に走らせる System を宣言する
     *
     * 突撃の状態が確定した後でないと1フレームずれる
     * DroneBrainSystem は別ステージ（L02）で既に手前なので書かない（書くとスケジューラが警告する）
     */
    std::vector<const char *> GetRunAfter() const override
    {
        return {"DroneMovementSystem"};
    }

    void Update(float dt) override;

    // 判定が開いている機数を出す。当たらないとき、判定が閉じているのか届いていないのかを切り分ける
    void OnGui() override;

private:
    // 判定を開く瞬間に、技の当たり方とダメージの帰属先を写し、命中の履歴を捨てる
    // 履歴を捨てないと、2回目の突撃が同じ相手に当たらない
    void OpenHitbox(HitboxComponent &box, const CCL::AI::Formation::FormationCombat &combat, CCL::ECS::EntityID owner) const;

    int m_droneCount    = 0;
    int m_activeCount   = 0;
    int m_stateCount[4] = {0, 0, 0, 0};
};
