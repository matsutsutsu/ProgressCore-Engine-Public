#include "JoltCharacterContactListener.h"
#include "Engine/Physics/Components/Collision/JoltCollisionEvent.h"
#include "Engine/Physics/JoltIntegration/PhysicsLayers.h" // レイヤー定義（責務分離済み）

using namespace CCL::ECS;

JoltCharacterContactListener::JoltCharacterContactListener(JoltContactListener *sink,
    EntityID                                                                    playerID,
    JPH::BodyInterface                                                         *bodyInterface)
    : m_sink(sink), m_playerID(playerID), m_bodyInterface(bodyInterface)
{
}

void JoltCharacterContactListener::OnContactAdded(const JPH::CharacterVirtual *inCharacter,
    const JPH::BodyID                                                         &inBodyID2,
    const JPH::SubShapeID                                                     &inSubShapeID2,
    JPH::RVec3Arg                                                              inContactPosition,
    JPH::Vec3Arg                                                               inContactNormal,
    JPH::CharacterContactSettings                                             &ioSettings)
{
    // =================================================================================
    // ★ イベントストームの遮断
    // 相手が「動かない背景（壁や床）」なら、イベントを発行せずここで即座に捨てる
    // =================================================================================
    JPH::ObjectLayer hitLayer = m_bodyInterface->GetObjectLayer(inBodyID2);

    if (hitLayer == PhysicsLayers::NON_MOVING)  return;

    // 相手の BodyID から ECSの EntityID を復元
    uint64_t userData    = m_bodyInterface->GetUserData(inBodyID2);
    EntityID hitEntityID = static_cast<EntityID>(userData);

    if (hitEntityID != 0 && hitEntityID != InvalidEntityID)
    {
        JoltCollisionEvent ev;
        ev.entityA         = m_playerID;
        ev.entityB         = hitEntityID;
        ev.contactPosition = {inContactPosition.GetX(), inContactPosition.GetY(), inContactPosition.GetZ()};
        ev.contactNormal   = {inContactNormal.GetX(), inContactNormal.GetY(), inContactNormal.GetZ()};

        JPH::Vec3 relVel    = inCharacter->GetLinearVelocity() - m_bodyInterface->GetLinearVelocity(inBodyID2);
        ev.relativeVelocity = {relVel.GetX(), relVel.GetY(), relVel.GetZ()};

        // ★fix: EventBus 即時発行をやめ、剛体側と同じバッファへ蓄積する。
        //   物理更新の途中でゲームロジックを割り込ませないための一貫化。
        if (m_sink) m_sink->PushEvent(ev);
    }
}