#include "JoltContactListener.h"
#include <Jolt/Physics/Body/Body.h>
#include <utility>

#include "Engine/Physics/JoltIntegration/HitDiagnostics.h"
#include "Engine/Physics/JoltIntegration/PhysicsLayers.h"

namespace
{
    // 攻撃判定に関わる接触か（どちらかがセンサー）。計測ログを絞るのに使う
    // 地面や壁との接触まで出すと、ログが埋まって読めない
    bool IsAttackContact(const JPH::Body &a, const JPH::Body &b)
    {
        return a.IsSensor() || b.IsSensor();
    }



    // 背景（壁・床・見えない壁）が絡む接触か。絡む接触は捨てる
    // OnContactPersisted は接触が続く限り毎フレーム来るので、地面に載る剛体だけでイベントのバッファが埋まる
    // 背景との接触を使う購読者はいない（攻撃レイヤーはそもそも壁と当たらない）
    bool IsStaticWorldContact(const JPH::Body &a, const JPH::Body &b)
    {
        return PhysicsLayers::IsStaticWorldLayer(a.GetObjectLayer()) || PhysicsLayers::IsStaticWorldLayer(b.GetObjectLayer());
    }
}

// ---------------------------------------------------------------------------
// イベントの作成
// ---------------------------------------------------------------------------

void JoltContactListener::EmitContactEvent(const JPH::Body &inBody1, const JPH::Body &inBody2,
                                           const JPH::ContactManifold &inManifold,
                                           const char                 *inPhase)
{
    // 1. 剛体の UserData に入れておいた EntityID を取り出す（0 は Entity を持たない剛体）
    auto entityA = static_cast<CCL::ECS::EntityID>(inBody1.GetUserData());
    auto entityB = static_cast<CCL::ECS::EntityID>(inBody2.GetUserData());

    // 両方とも Entity を持たないなら無視する
    if (entityA == 0 && entityB == 0) return;

    // 背景が絡む接触はイベント化しない（上の IsStaticWorldContact のコメント参照）
    if (IsStaticWorldContact(inBody1, inBody2)) return;

    // 2. 接触点・法線・相対速度を求める（接触点は1点目だけを使う）
    JPH::RVec3 pos    = inManifold.mBaseOffset + inManifold.mRelativeContactPointsOn1[0];
    JPH::Vec3  normal = inManifold.mWorldSpaceNormal;
    JPH::Vec3  relVel = inBody1.GetLinearVelocity() - inBody2.GetLinearVelocity();

    JoltCollisionEvent newEvent;
    newEvent.entityA          = entityA;
    newEvent.entityB          = entityB;
    newEvent.contactPosition  = {pos.GetX(), pos.GetY(), pos.GetZ()};
    newEvent.contactNormal    = {normal.GetX(), normal.GetY(), normal.GetZ()};
    newEvent.relativeVelocity = {relVel.GetX(), relVel.GetY(), relVel.GetZ()};

    // 3. mutex で守ったバッファへ積む
    PushEvent(newEvent);

    if (IsAttackContact(inBody1, inBody2))
    {
        HITDIAG_LOG(LogCategory::Physics,
                    "[HitDiag/Contact] Body %-7s A=%llu B=%llu (body %u/%u, sensor %d/%d) -> イベント発行",
                    inPhase, entityA, entityB,
                    inBody1.GetID().GetIndex(), inBody2.GetID().GetIndex(),
                    inBody1.IsSensor() ? 1 : 0, inBody2.IsSensor() ? 1 : 0);
    }
}



// ---------------------------------------------------------------------------
// Jolt からの通知
// ---------------------------------------------------------------------------

void JoltContactListener::OnContactAdded(const JPH::Body &inBody1, const JPH::Body &inBody2,
                                         const JPH::ContactManifold &inManifold,
                                         JPH::ContactSettings & /*ioSettings*/)
{
    EmitContactEvent(inBody1, inBody2, inManifold, "Add");
}



void JoltContactListener::OnContactPersisted(const JPH::Body &inBody1, const JPH::Body &inBody2,
                                             const JPH::ContactManifold &inManifold,
                                             JPH::ContactSettings & /*ioSettings*/)
{
    // Added と同じに扱う。判定の区間が開く前に剣が入っていると、区間の中には Persisted しか来ない
    // 多段ヒットは HitboxCollisionSystem の AlreadyHit が防ぐので、毎フレーム流してよい
    EmitContactEvent(inBody1, inBody2, inManifold, "Persist");
}



void JoltContactListener::OnContactRemoved(const JPH::SubShapeIDPair &inSubShapePair)
{
    // Body へ触れないので BodyID の index だけ。Add/Persist のログと突き合わせる
    HITDIAG_LOG(LogCategory::Physics,
                "[HitDiag/Contact] Body Remove  body %u/%u",
                inSubShapePair.GetBody1ID().GetIndex(),
                inSubShapePair.GetBody2ID().GetIndex());
}



// ---------------------------------------------------------------------------
// イベントの受け渡し
// ---------------------------------------------------------------------------

void JoltContactListener::PushEvent(const JoltCollisionEvent &ev)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_events.push_back(ev);
}



std::vector<JoltCollisionEvent> JoltContactListener::GetAndClearEvents()
{
    // コピーではなく swap で受け渡す。ロックを持つのは中身の入れ替えの間だけで済み、要素のコピーも起きない
    std::vector<JoltCollisionEvent> out;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::swap(out, m_events); // m_events は空の状態になる
    }
    return out; // NRVO / ムーブ
}