#include "JoltContactListener.h"
#include <Jolt/Physics/Body/Body.h> // Bodyの中身(完全な型)を知るために絶対に必要
#include <utility>                  // std::swap

void JoltContactListener::OnContactAdded(const JPH::Body& inBody1, const JPH::Body& inBody2,
                                         const JPH::ContactManifold& inManifold,
                                         JPH::ContactSettings& ioSettings)
{
    // 1. Joltの剛体に埋め込んでおいた ECSの EntityID を取り出す
    auto entityA = static_cast<CCL::ECS::EntityID>(inBody1.GetUserData());
    auto entityB = static_cast<CCL::ECS::EntityID>(inBody2.GetUserData());

    // 両方とも正しくEntityIDが取れなかった場合は無視（壁同士など）
    if (entityA == 0 && entityB == 0) return;

    // 2. 衝突座標と法線を計算
    JPH::RVec3 pos    = inManifold.mBaseOffset + inManifold.mRelativeContactPointsOn1[0];
    JPH::Vec3  normal = inManifold.mWorldSpaceNormal;
    JPH::Vec3  relVel = inBody1.GetLinearVelocity() - inBody2.GetLinearVelocity();

    JoltCollisionEvent newEvent;
    newEvent.entityA          = entityA;
    newEvent.entityB          = entityB;
    newEvent.contactPosition  = {pos.GetX(), pos.GetY(), pos.GetZ()};
    newEvent.contactNormal    = {normal.GetX(), normal.GetY(), normal.GetZ()};
    newEvent.relativeVelocity = {relVel.GetX(), relVel.GetY(), relVel.GetZ()};

    // 3. スレッドセーフに配列へ追加
    PushEvent(newEvent);
}

void JoltContactListener::PushEvent(const JoltCollisionEvent& ev)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_events.push_back(ev);
}

std::vector<JoltCollisionEvent> JoltContactListener::GetAndClearEvents()
{
    // fix: コピーではなく swap で受け渡す。
    //   ロックを保持する時間はポインタ入れ替えの一瞬だけで済み、コピーコストもゼロになる。
    std::vector<JoltCollisionEvent> out;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::swap(out, m_events); // m_events は空の状態になる
    }
    return out; // NRVO / ムーブ
}