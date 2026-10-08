#include "JoltCharacterContactListener.h"
#include "Engine/Physics/Components/Collision/JoltCollisionEvent.h"
#include "Engine/Physics/JoltIntegration/PhysicsLayers.h"
#include "Engine/Physics/JoltIntegration/HitDiagnostics.h" // 計測ログ

using namespace CCL::ECS;

bool JoltCharacterContactListener::IsAttackLayer(const JPH::BodyID &bodyID) const
{
    if (!m_bodyInterface)
        return false;

    const JPH::ObjectLayer layer = m_bodyInterface->GetObjectLayer(bodyID);
    return layer == PhysicsLayers::PLAYER_ATTACK || layer == PhysicsLayers::BOSS_ATTACK;
}

JoltCharacterContactListener::JoltCharacterContactListener(JoltContactListener *sink,
                                                           EntityID             selfID,
                                                           JPH::BodyInterface  *bodyInterface)
    : m_sink(sink), m_selfID(selfID), m_bodyInterface(bodyInterface)
{
}

void JoltCharacterContactListener::EmitContactEvent(const JPH::CharacterVirtual *inCharacter,
                                                    const JPH::BodyID           &inBodyID2,
                                                    JPH::RVec3Arg                inContactPosition,
                                                    JPH::Vec3Arg                 inContactNormal,
                                                    const char                  *inPhase)
{
    // 相手が動かない背景（壁・床・落下防止の見えない壁）なら、イベントを出さずに捨てる。
    //   接触が続く限り毎フレーム呼ばれるので、背景を通すとバッファがそれで埋まる。
    //   判定を PhysicsLayers::IsStaticWorldLayer に任せているのは、レイヤーを増やした
    //   ときにここの条件を足し忘れないため（NON_MOVING だけを直書きすると INVISIBLE_WALL が漏れる）。
    JPH::ObjectLayer hitLayer = m_bodyInterface->GetObjectLayer(inBodyID2);

    if (PhysicsLayers::IsStaticWorldLayer(hitLayer)) return;

    // 相手の BodyID から ECSの EntityID を復元
    uint64_t userData    = m_bodyInterface->GetUserData(inBodyID2);
    EntityID hitEntityID = static_cast<EntityID>(userData);

    if (IsAttackLayer(inBodyID2))
    {
        HITDIAG_LOG(LogCategory::Physics,
                    "[HitDiag/Contact] Char %-7s self=%llu other=%llu (body %u, layer %d) -> イベント発行",
                    inPhase, m_selfID, hitEntityID, inBodyID2.GetIndex(), static_cast<int>(hitLayer));
    }

    if (hitEntityID != 0 && hitEntityID != InvalidEntityID)
    {
        JoltCollisionEvent ev;
        ev.entityA         = m_selfID;
        ev.entityB         = hitEntityID;
        ev.contactPosition = {inContactPosition.GetX(), inContactPosition.GetY(), inContactPosition.GetZ()};
        ev.contactNormal   = {inContactNormal.GetX(), inContactNormal.GetY(), inContactNormal.GetZ()};

        JPH::Vec3 relVel    = inCharacter->GetLinearVelocity() - m_bodyInterface->GetLinearVelocity(inBodyID2);
        ev.relativeVelocity = {relVel.GetX(), relVel.GetY(), relVel.GetZ()};

        // EventBus へ直接出さず、剛体側と同じバッファへ積む（物理更新の途中でゲームロジックを走らせない）
        if (m_sink) m_sink->PushEvent(ev);
    }
}

void JoltCharacterContactListener::OnContactAdded(const JPH::CharacterVirtual *inCharacter,
                                                  const JPH::BodyID           &inBodyID2,
                                                  const JPH::SubShapeID & /*inSubShapeID2*/,
                                                  JPH::RVec3Arg inContactPosition,
                                                  JPH::Vec3Arg  inContactNormal,
                                                  JPH::CharacterContactSettings & /*ioSettings*/)
{
    EmitContactEvent(inCharacter, inBodyID2, inContactPosition, inContactNormal, "Add");
}

void JoltCharacterContactListener::OnContactPersisted(const JPH::CharacterVirtual *inCharacter,
                                                      const JPH::BodyID           &inBodyID2,
                                                      const JPH::SubShapeID & /*inSubShapeID2*/,
                                                      JPH::RVec3Arg inContactPosition,
                                                      JPH::Vec3Arg  inContactNormal,
                                                      JPH::CharacterContactSettings & /*ioSettings*/)
{
    // Added と全く同じ扱いにする。CharacterVirtual は前フレームの接触を
    // mListenerContacts に持ち越し、既知の対を Added ではなく Persisted に回すため
    // (CharacterVirtual.cpp の ContactAdded)、ここを実装しないと重なり続けている間の
    // 接触が全て消える。多段ヒットは AlreadyHit が防ぐので毎フレーム流して良い。
    EmitContactEvent(inCharacter, inBodyID2, inContactPosition, inContactNormal, "Persist");
}

void JoltCharacterContactListener::OnContactRemoved(const JPH::CharacterVirtual * /*inCharacter*/,
                                                    const JPH::BodyID &inBodyID2,
                                                    const JPH::SubShapeID & /*inSubShapeID2*/)
{
    if (!IsAttackLayer(inBodyID2))
        return;

    // これが出れば接触は解放済みで、次の接近は Added として来る。
    HITDIAG_LOG(LogCategory::Physics,
                "[HitDiag/Contact] Char Remove  self=%llu (body %u)",
                m_selfID, inBodyID2.GetIndex());
}