#include "JoltCharacterContactListener.h"
#include "Engine/Physics/Components/Collision/JoltCollisionEvent.h"
#include "Engine/Physics/JoltIntegration/PhysicsLayers.h"
#include "Engine/Physics/JoltIntegration/HitDiagnostics.h" // 計測ログ

using namespace CCL::ECS;



// 攻撃レイヤー（PLAYER_ATTACK / BOSS_ATTACK）との接触か
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



// 接触1件をイベントにして、剛体側と同じバッファへ積む
void JoltCharacterContactListener::EmitContactEvent(const JPH::CharacterVirtual *inCharacter,
                                                    const JPH::BodyID           &inBodyID2,
                                                    JPH::RVec3Arg                inContactPosition,
                                                    JPH::Vec3Arg                 inContactNormal,
                                                    const char                  *inPhase)
{
    // 相手が動かない背景（壁・床・見えない壁）なら捨てる
    // 接触が続く限り毎フレーム呼ばれるので、通すとバッファが背景で埋まる
    JPH::ObjectLayer hitLayer = m_bodyInterface->GetObjectLayer(inBodyID2);

    if (PhysicsLayers::IsStaticWorldLayer(hitLayer)) return;

    // 相手の UserData から EntityID を取り出す
    uint64_t userData    = m_bodyInterface->GetUserData(inBodyID2);
    EntityID hitEntityID = static_cast<EntityID>(userData);

    if (IsAttackLayer(inBodyID2))
    {
        HITDIAG_LOG(LogCategory::Physics,
                    "[HitDiag/Contact] Char %-7s self=%llu other=%llu (body %u, layer %d) -> イベント発行",
                    inPhase, m_selfID, hitEntityID, inBodyID2.GetIndex(), static_cast<int>(hitLayer));
    }

    // Entity を持つ相手だけ、イベントにして積む
    // EventBus へは出さない（物理更新の途中でゲームロジックを走らせない）
    if (hitEntityID != 0 && hitEntityID != InvalidEntityID)
    {
        JoltCollisionEvent ev;
        ev.entityA         = m_selfID;
        ev.entityB         = hitEntityID;
        ev.contactPosition = {inContactPosition.GetX(), inContactPosition.GetY(), inContactPosition.GetZ()};
        ev.contactNormal   = {inContactNormal.GetX(), inContactNormal.GetY(), inContactNormal.GetZ()};

        JPH::Vec3 relVel    = inCharacter->GetLinearVelocity() - m_bodyInterface->GetLinearVelocity(inBodyID2);
        ev.relativeVelocity = {relVel.GetX(), relVel.GetY(), relVel.GetZ()};

        if (m_sink) m_sink->PushEvent(ev);
    }
}



// 接触が始まった
void JoltCharacterContactListener::OnContactAdded(const JPH::CharacterVirtual *inCharacter,
                                                  const JPH::BodyID           &inBodyID2,
                                                  const JPH::SubShapeID & /*inSubShapeID2*/,
                                                  JPH::RVec3Arg inContactPosition,
                                                  JPH::Vec3Arg  inContactNormal,
                                                  JPH::CharacterContactSettings & /*ioSettings*/)
{
    EmitContactEvent(inCharacter, inBodyID2, inContactPosition, inContactNormal, "Add");
}



// 接触が続いている。Added と同じ扱いにする
void JoltCharacterContactListener::OnContactPersisted(const JPH::CharacterVirtual *inCharacter,
                                                      const JPH::BodyID           &inBodyID2,
                                                      const JPH::SubShapeID & /*inSubShapeID2*/,
                                                      JPH::RVec3Arg inContactPosition,
                                                      JPH::Vec3Arg  inContactNormal,
                                                      JPH::CharacterContactSettings & /*ioSettings*/)
{
    // CharacterVirtual は前フレームから続く接触を、Added ではなくこちらで呼ぶ
    // 毎フレーム流しても、多段ヒットは AlreadyHit が防ぐ
    EmitContactEvent(inCharacter, inBodyID2, inContactPosition, inContactNormal, "Persist");
}



// 接触が終わった。攻撃レイヤーなら計測ログだけ出す
void JoltCharacterContactListener::OnContactRemoved(const JPH::CharacterVirtual * /*inCharacter*/,
                                                    const JPH::BodyID &inBodyID2,
                                                    const JPH::SubShapeID & /*inSubShapeID2*/)
{
    if (!IsAttackLayer(inBodyID2))
        return;

    // これが出れば接触は解放済みで、次の接近は Added として来る
    HITDIAG_LOG(LogCategory::Physics,
                "[HitDiag/Contact] Char Remove  self=%llu (body %u)",
                m_selfID, inBodyID2.GetIndex());
}
