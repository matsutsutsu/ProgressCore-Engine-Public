#include "JoltContactListener.h"
#include <Jolt/Physics/Body/Body.h> // Bodyの中身(完全な型)を知るために絶対に必要
#include <utility>                  // std::swap

#include "Engine/Physics/JoltIntegration/HitDiagnostics.h" // 計測ログ(暫定)
#include "Engine/Physics/JoltIntegration/PhysicsLayers.h"   // IsStaticWorldLayer

namespace
{
// 攻撃判定に関わる接触だけを計測対象にする。
//
// 地面や壁との接触まで出すとログが埋まって読めなくなるため、
// センサー(＝Hitbox の剣)が絡む対だけに絞る。
bool IsAttackContact(const JPH::Body &a, const JPH::Body &b)
{
    return a.IsSensor() || b.IsSensor();
}

// 背景（壁・床・見えない壁）が絡む接触か。
//
// OnContactPersisted は接触が続く限り毎フレーム呼ばれるため、地面に載っている
// 剛体があるだけでイベントバッファが背景との接触で埋まる。購読側
// (HitboxCollisionSystem / JustEvadeSystem) は全イベントを自前の vector へ
// コピーしてから絞り込むので、コストは二重に乗る。
//
// 背景との接触を必要とする購読者は存在しない（攻撃レイヤーは pair フィルタで
// そもそも壁と当たらない）ので、ここで捨てて良い。
// キャラ側の JoltCharacterContactListener には元から同じ遮断があり、
// 剛体側にだけ無かったものを揃える。
bool IsStaticWorldContact(const JPH::Body &a, const JPH::Body &b)
{
    return PhysicsLayers::IsStaticWorldLayer(a.GetObjectLayer())
           || PhysicsLayers::IsStaticWorldLayer(b.GetObjectLayer());
}
} // namespace

void JoltContactListener::EmitContactEvent(const JPH::Body& inBody1, const JPH::Body& inBody2,
                                           const JPH::ContactManifold& inManifold,
                                           const char* inPhase)
{
    // 1. Joltの剛体に埋め込んでおいた ECSの EntityID を取り出す
    auto entityA = static_cast<CCL::ECS::EntityID>(inBody1.GetUserData());
    auto entityB = static_cast<CCL::ECS::EntityID>(inBody2.GetUserData());

    // 両方とも正しくEntityIDが取れなかった場合は無視（壁同士など）
    if (entityA == 0 && entityB == 0) return;

    // 背景が絡む接触はイベント化しない（上の IsStaticWorldContact のコメント参照）
    if (IsStaticWorldContact(inBody1, inBody2)) return;

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

    if (IsAttackContact(inBody1, inBody2))
    {
        HITDIAG_LOG(LogCategory::Physics,
            "[HitDiag/Contact] Body %-7s A=%llu B=%llu (body %u/%u, sensor %d/%d) -> イベント発行",
            inPhase, entityA, entityB,
            inBody1.GetID().GetIndex(), inBody2.GetID().GetIndex(),
            inBody1.IsSensor() ? 1 : 0, inBody2.IsSensor() ? 1 : 0);
    }
}

void JoltContactListener::OnContactAdded(const JPH::Body& inBody1, const JPH::Body& inBody2,
                                         const JPH::ContactManifold& inManifold,
                                         JPH::ContactSettings& /*ioSettings*/)
{
    EmitContactEvent(inBody1, inBody2, inManifold, "Add");
}

void JoltContactListener::OnContactPersisted(const JPH::Body& inBody1, const JPH::Body& inBody2,
                                             const JPH::ContactManifold& inManifold,
                                             JPH::ContactSettings& /*ioSettings*/)
{
    // Added と全く同じ扱いにする。Jolt は同じボディ対の2回目以降を Persisted に回すため
    // (ContactListener.h の注記)、ここを実装しないと「重なり続けている間」の接触が
    // 全て消える。攻撃判定の窓が開くより先に剣が相手へ入っていると Added は
    // isActive=false のときに消費済みで、窓の中には Persisted しか残らず不発になる。
    // 多段ヒットは HitboxCollisionSystem の AlreadyHit が防ぐので毎フレーム流して良い。
    EmitContactEvent(inBody1, inBody2, inManifold, "Persist");
}

void JoltContactListener::OnContactRemoved(const JPH::SubShapeIDPair& inSubShapePair)
{
    // Body へ触れないので BodyID の index だけ。Add/Persist のログと突き合わせる。
    HITDIAG_LOG(LogCategory::Physics,
        "[HitDiag/Contact] Body Remove  body %u/%u",
        inSubShapePair.GetBody1ID().GetIndex(),
        inSubShapePair.GetBody2ID().GetIndex());
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