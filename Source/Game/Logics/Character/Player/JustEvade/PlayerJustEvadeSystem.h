#pragma once
// ===========================================================================
// 概要: 回避ダッシュ中の接触から、ジャスト回避の成立を判定する System。
//
// 責務:
//   - 物理の接触イベント(JoltCollisionEvent)を溜める
//   - Update で、受付窓が開いている間に「攻撃中の敵」へ触れたら成立とみなす
//   - 成立したら JustEvadeSuccessEvent を発行する（受け取った PlayerRushSystem がラッシュを始める）
//
// 備考: イベントは発行元(JoltCollisionEventSystem)のタスク上で届くので、その場で判定すると
//       この System の Write 宣言がスケジューラから見えなくなる。溜めて Update で判定する。
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Game/Logics/Character/Player/JustEvade/PlayerJustEvadeComponent.h"
#include "Game/Logics/Character/Player/Dash/PlayerDashComponent.h"
#include "Engine/Physics/Components/Collision/JoltCollisionEvent.h"

#include <vector>

class PlayerJustEvadeSystem : public CCL::ECS::IfSystem<PlayerJustEvadeSystem,
                                                  CCL::ECS::Write<PlayerJustEvadeComponent>,
                                                  CCL::ECS::Write<PlayerDashComponent>>
{
public:
    PlayerJustEvadeSystem() : IfSystem("PlayerJustEvadeSystem") {}

    void Initialize() override;
    void Update(float dt) override;

    // 接触の配送(JoltCollisionEventSystem)が終わってから判定する（同じ L05_Collision のため順序を宣言）。
    std::vector<const char *> GetRunAfter() const override { return {"JoltCollisionEventSystem"}; }

private:
    // 接触した2体について、playerCandidate 側がプレイヤーなら成立するか調べる（向きを入れ替えて2回呼ぶ）。
    void CheckJustEvade(CCL::ECS::EntityID playerCandidate, CCL::ECS::EntityID otherCandidate);

    // このフレームに届いた接触。コールバックが溜め、Update が処理して空にする。
    std::vector<JoltCollisionEvent> m_pendingContacts;
};
