#include "SpawnHitboxSystem.h"

#include "Engine/Animation/Core/AnimEventMessages.h"
#include "CombatComponents.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/Physics/Components/RigidBody/JoltRigidBodyComponent.h"
#include "Engine/Physics/Components/Collision/JoltSphereColliderComponent.h"
#include "Engine/Physics/JoltIntegration/PhysicsLayers.h"

#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/Common/CCL_Archetype.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include "Engine/Platform/Core/Logger.h"

#include <DirectXMath.h>

using namespace CCL::ECS;
using namespace DirectX;

namespace
{
    // 寿命が 0 で来たときに使う最短の寿命（秒）。
    // FixedUpdate（60Hz）で3フレームほどあり、センサーが重なりを1回は見つけられる
    constexpr float kMinHitboxLifetime = 0.05f;
} // namespace

void SpawnHitboxSystem::Initialize()
{
    // AnimationSystem（L02_PostUpdate）が Publish し、その中でこのコールバックが呼ばれる。
    // Spawn はすぐには反映されず、後でまとめて反映される
    ListenEvent<AnimEventSpawnHitboxMessage>([this](const AnimEventSpawnHitboxMessage &msg) {
        OnSpawnHitbox(msg);
    });
}

void SpawnHitboxSystem::Update(float dt)
{
    // 作った判定の寿命を減らし、尽きたら消す（反映は次の ScrutinyAndApply。剛体は JoltCleanupSystem が片付ける）
    ForEachWithID([&](CCL::ECS::EntityID id, HitboxLifetimeComponent &life) {
        life.remainingLife -= dt;
        if (life.remainingLife <= 0.0f)
            m_world->Destroy(id);
    });
}

void SpawnHitboxSystem::OnSpawnHitbox(const AnimEventSpawnHitboxMessage &msg)
{
    // 1. 持ち主の Transform。無ければ位置が決まらないので作らない
    const auto *ownerT = m_world->GetComponent<TransformComponent>(msg.owner);
    if (!ownerT)
    {
        CCL_LOG_WARN(LogCategory::Game,
                     "[SpawnHitbox] owner %llu に Transform 無し。生成中止", msg.owner);
        return;
    }

    // 2. offset を持ち主の回転で回して、作る位置（ワールド座標）を求める
    const XMFLOAT3 ownerPos = ownerT->GetWorldPosition();
    const XMFLOAT4 ownerRot = ownerT->GetWorldRotation();

    const XMVECTOR off        = XMVectorSet(msg.offset.x, msg.offset.y, msg.offset.z, 0.0f);
    const XMVECTOR rotatedOff = XMVector3Rotate(off, XMLoadFloat4(&ownerRot));
    const XMVECTOR spawnPos   = XMVectorAdd(XMLoadFloat3(&ownerPos), rotatedOff);

    XMFLOAT3 spawnPosF;
    XMStoreFloat3(&spawnPosF, spawnPos);

    // 3. 誰に当てるかは、イベントの衝突レイヤーで決める（Sequencer の SpawnHitbox で選ぶ）。
    //    PLAYER_ATTACK ならボスに、BOSS_ATTACK ならプレイヤーに当たる
    const JPH::ObjectLayer layer = static_cast<JPH::ObjectLayer>(msg.attackLayer);

    // 4. エンティティを作る
    static const Archetype hitboxArch = ArchetypeHelper::Generate<
        TransformComponent, HitboxComponent, JoltRigidbodyComponent,
        JoltSphereColliderComponent, HitboxLifetimeComponent>();
    auto ref = m_world->Spawn(hitboxArch);

    // 親を持たないのでローカル座標がそのままワールド座標になる。作った直後は worldMatrix が単位行列のままで、
    // JoltBodySetupSystem（L03）は GetWorldPosition() を読むので、そのままだと剛体が原点に作られる。
    // ここで worldMatrix を求めておく
    TransformComponent t{};
    t.position = spawnPosF;
    t.rotation = ownerRot; // 球なので回転は効かないが、向きをそろえておく
    t.isDirty  = true;
    t.UpdateMatrix();
    ref.Set(t);

    // 作った時から判定を開いておく。ownerEntity は Invalid のままなので、CombatAnimationSyncSystem は触らない
    HitboxComponent hb{};
    hb.ownerID         = msg.owner;
    hb.damageAmount    = msg.damage;
    hb.knockback       = msg.knockback;
    hb.isActive        = true;
    hb.hitStopDuration = msg.hitStopDuration;
    hb.hitStopScale    = msg.hitStopScale;
    ref.Set(hb);

    // Kinematic のセンサーにする。すり抜けたまま接触だけを知らせ、判定は HitboxCollisionSystem に任せる
    JoltRigidbodyComponent rb{};
    rb.motionType    = JPH::EMotionType::Kinematic;
    rb.objectLayer   = layer;
    rb.isSensor      = true;
    rb.gravityFactor = 0.0f;
    ref.Set(rb);

    JoltSphereColliderComponent sc{};
    sc.radius = msg.radius;
    ref.Set(sc);

    HitboxLifetimeComponent life{};
    life.remainingLife = (msg.lifeTime > 0.0f) ? msg.lifeTime : kMinHitboxLifetime;
    ref.Set(life);

    CCL_LOG_INFO(LogCategory::Game,
                 "[SpawnHitbox] owner=%llu pos=(%.2f,%.2f,%.2f) r=%.2f dmg=%.1f life=%.2fs layer=%d",
                 msg.owner, spawnPosF.x, spawnPosF.y, spawnPosF.z,
                 msg.radius, msg.damage, life.remainingLife, static_cast<int>(layer));
}

// 作るのはイベントで行うので、ステージは寿命を減らすタイミングだけに関わる。
// 当たり判定と同じ L05_Collision に置く
REGISTER_LOGIC_SYSTEM(SpawnHitboxSystem, Priority::LogicStage::L05_Collision);
