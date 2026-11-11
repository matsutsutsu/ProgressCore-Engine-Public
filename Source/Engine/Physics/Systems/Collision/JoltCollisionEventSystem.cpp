#include "JoltCollisionEventSystem.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/Physics/JoltIntegration/JoltPhysicsManager.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

using namespace CCL::ECS;

void JoltCollisionEventSystem::Update(float dt)
{
    if (!m_world->HasResource<JoltPhysicsManager *>()) return;

    auto                &joltManager = m_world->GetResource<JoltPhysicsManager *>();
    JoltContactListener *listener    = joltManager->GetContactListener();
    if (!listener) return;

    // バッファを丸ごと取り出して空にする（中では swap するだけなのでコピーは起きない）
    std::vector<JoltCollisionEvent> events = listener->GetAndClearEvents();

    // 物理更新は終わっているので、ここからならゲームロジックへ安全に渡せる
    for (const auto &ev : events)
    {
        m_world->GetEventBus().Publish(ev);
    }
}



// 物理(L04_Physics)の直後、当たり判定処理の先頭で動かす
REGISTER_LOGIC_SYSTEM(JoltCollisionEventSystem, Priority::LogicStage::L05_Collision);