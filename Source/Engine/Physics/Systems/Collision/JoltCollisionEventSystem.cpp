// JoltContactListenerに蓄積された衝突イベントを回収し、ECSのEventBusへ配送するシステム
//
// このシステムは必ずJoltの物理更新（Step）が完了した後の L05_Collision ステージで実行されなければならない。
#include "JoltCollisionEventSystem.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/Physics/JoltIntegration/JoltPhysicsManager.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

using namespace CCL::ECS;

void JoltCollisionEventSystem::Update(float dt)
{
    // リソースの存在確認（初期化前などのクラッシュ防止）
    if (!m_world->HasResource<JoltPhysicsManager *>()) return;

    auto                &joltManager = m_world->GetResource<JoltPhysicsManager *>();
    JoltContactListener *listener    = joltManager->GetContactListener();
    if (!listener) return;

    // 1. 【一括回収】ポストから全ての手紙を取り出し、ポストを空にする。
    // ※内部で一時的にロックがかかるが、std::vectorのムーブセマンティクスなどを
    // 活用すればメモリコピーのコストは最小化される。
    std::vector<JoltCollisionEvent> events = listener->GetAndClearEvents();

    // 2. 【配送】回収した手紙をECSのEventBusに流す。
    // ここはメインスレッド（またはECSの制御下）なので、安全に後続のシステムへ情報を渡せる。
    for (const auto &ev : events)
    {
        // EventBusを通じて、HitboxCollisionSystemなどがこれを受信する
        m_world->GetEventBus().Publish(ev);
    }
}

// 物理エンジン(L04_Physics)の直後、当たり判定処理の先頭として登録
REGISTER_LOGIC_SYSTEM(JoltCollisionEventSystem, Priority::LogicStage::L05_Collision);