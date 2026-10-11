#include "JustEvadeSystem.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include "Engine/Platform/Core/Logger.h"
#include "Engine/Core/Components/EntityLabel.h"

#include "Game/Core/GameEvents.h"                                      // JustEvadeSuccessEvent
#include "Game/Logics/AI/Drone/Components/DroneComponent.h"            // 突撃中のドローンか
#include "Game/Logics/Combat/Components/CombatComponents.h"            // HitboxComponent（攻撃判定が開いているか）
#include "Engine/Physics/Components/RigidBody/JoltRigidBodyComponent.h" // 壁/地形の除外（レイヤー判定）

using namespace CCL::ECS;

void JustEvadeSystem::Initialize()
{
    // 物理の接触を購読する。判定はここでは行わず、溜めるだけ（Write 宣言を活かすため）。
    ListenEvent<JoltCollisionEvent>([this](const JoltCollisionEvent &ev) {
        m_pendingContacts.push_back(ev);
    });
}

void JustEvadeSystem::Update(float /*dt*/)
{
    // 1. このフレームの接触を判定する（A/B どちらがプレイヤーでもよいよう両方向で調べる）。
    for (const JoltCollisionEvent &ev : m_pendingContacts)
    {
        CheckJustEvade(ev.entityA, ev.entityB);
        CheckJustEvade(ev.entityB, ev.entityA);
    }
    m_pendingContacts.clear();

    // 2. 成立フラグは「このダッシュで成立済みか」を表すので、ダッシュしていない間は下ろす。
    // ここではジャスト回避中かどうかだけを見る
    ForEach([&](JustEvadeComponent &evade, DashComponent &dash) {
        if (!dash.isDashing)
        {
            evade.isJustSuccess = false;
        }
    });
}

void JustEvadeSystem::CheckJustEvade(EntityID playerCandidate, EntityID otherCandidate)
{
    auto *evade = m_world->GetComponent<JustEvadeComponent>(playerCandidate);
    auto *dash  = m_world->GetComponent<DashComponent>(playerCandidate);
    if (!evade || !dash) return;

    // 受付窓が開いていて、このダッシュでまだ成立していないときだけ。
    if (!dash->isJustEvadeWindowActive) return;
    if (evade->isJustSuccess) return;

    // 壁や地形（動かない物）との接触では成立させない。
    if (auto *otherBody = m_world->GetComponent<JoltRigidbodyComponent>(otherCandidate))
    {
        if (otherBody->objectLayer == PhysicsLayers::NON_MOVING ||
            otherBody->objectLayer == PhysicsLayers::INVISIBLE_WALL)
            return;
    }

    // 相手が「今まさに有効な攻撃」のときだけ成立させる。
    //   ドローン         : 突撃中(FireCharge)のみ
    //   Hitbox(ボスの剣) : 判定が開いているときのみ
    //   どちらでもない相手（ボス本体・障害物）は攻撃ではないので成立しない。
    if (auto *otherDrone = m_world->GetComponent<DroneComponent>(otherCandidate))
    {
        if (otherDrone->currentState != DroneState::FireCharge) return;
    }
    else if (auto *otherHitbox = m_world->GetComponent<HitboxComponent>(otherCandidate))
    {
        if (!otherHitbox->isActive) return;
    }
    else
    {
        return;
    }

    // 成立。受付を閉じ、成立フラグを立て、イベントを発行する。
    dash->isJustEvadeWindowActive = false;
    evade->isJustSuccess          = true;

    m_world->GetEventBus().Publish(JustEvadeSuccessEvent{playerCandidate, otherCandidate});

    CCL_LOG_SUCCESS(LogCategory::Game, "JUST EVADE SUCCESS!! Player: %s",
                    CCL::EntityLabel(*m_world, playerCandidate).c_str());
}

// 接触の配送と同じ L05_Collision に置き、GetRunAfter でその後に走らせる。
REGISTER_LOGIC_SYSTEM(JustEvadeSystem, Priority::LogicStage::L05_Collision);
