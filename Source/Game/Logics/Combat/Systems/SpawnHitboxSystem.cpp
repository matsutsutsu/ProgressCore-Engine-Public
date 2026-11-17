#include "Game/Logics/Combat/Systems/SpawnHitboxSystem.h"

#include "Engine/Animation/Core/AnimEventMessages.h"
#include "Engine/Animation/Components/AnimatorComponent.h"
#include "Game/Logics/Combat/Components/CombatComponents.h"
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
    // 寿命が 0 で来たときに使う最短の寿命（秒）
    // FixedUpdate（60Hz）で3フレームほどあり、センサーが重なりを1回は見つけられる
    constexpr float kMinHitboxLifetime = 0.05f;
}

void SpawnHitboxSystem::Initialize()
{
    // AnimationSystem（L02_PostUpdate）が Publish し、その中でこのコールバックが呼ばれる
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
        {
            m_world->Destroy(id);
            return;
        }

        // 追従する判定は、持ち主が消えた・アニメーションが切り替わったら寿命の途中でも消す
        // 持ち主の Animator は読むだけ（Animator を書く System は L02 で、ステージが分かれている）
        if (const auto *follow = m_world->GetComponent<HitboxLifeAnimComponent>(id))
        {
            const auto *animator = m_world->GetComponent<AnimatorComponent>(follow->owner);
            if (!animator || animator->currentSequence != follow->sequence)
                m_world->Destroy(id);
        }
    });
}



void SpawnHitboxSystem::OnSpawnHitbox(const AnimEventSpawnHitboxMessage &msg)
{
    // 1. 持ち主の Transform。なければ位置が決まらないので作らない
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

    // 3. 誰に当てるかは、イベントの衝突レイヤーで決める（Sequencer の SpawnHitbox で選ぶ）
    //    PLAYER_ATTACK ならボスに、BOSS_ATTACK ならプレイヤーに当たる
    const JPH::ObjectLayer layer = static_cast<JPH::ObjectLayer>(msg.attackLayer);

    // 4. エンティティを作る。追従する判定だけ、アニメーションの切り替わりを見るためのコンポーネントを足す
    static const Archetype hitboxArch = ArchetypeHelper::Generate<
        TransformComponent, HitboxComponent, JoltRigidbodyComponent,
        JoltSphereColliderComponent, HitboxLifetimeComponent>();
    static const Archetype followHitboxArch = ArchetypeHelper::Generate<
        TransformComponent, HitboxComponent, JoltRigidbodyComponent,
        JoltSphereColliderComponent, HitboxLifetimeComponent, HitboxLifeAnimComponent>();
    auto ref = m_world->Spawn(msg.follow ? followHitboxArch : hitboxArch);

    // 作った直後は worldMatrix が単位行列のままで、JoltBodySetupSystem（L03）は GetWorldPosition() を読むので、
    // そのままだと剛体が原点に作られる。ここで worldMatrix を求めておく
    TransformComponent t{};
    if (msg.follow)
    {
        // 持ち主の子にする。position はローカル座標で、回転は持ち主に任せる
        // 以降は TransformUpdateSystem が親の動きを反映し、JoltPushSystem が Kinematic の剛体を合わせる
        // 親の子リストへの登録は、TransformUpdateSystem が新しい子を見つけたときに行う
        //
        // 子の position には親の scale も掛かる（Valkyra は 5 倍）。一方、球の半径には scale が掛からず、
        // 追従しない判定の offset もメートルのまま使う。そろえるため、offset を持ち主のワールドの scale で割り、
        // どちらのモードでも offset が「ワールドのメートル」になるようにする
        const XMMATRIX ownerWorld = XMLoadFloat4x4(&ownerT->worldMatrix);
        XMVECTOR       ownerScale, ownerRotQ, ownerTrans;
        XMMatrixDecompose(&ownerScale, &ownerRotQ, &ownerTrans, ownerWorld);
        XMFLOAT3 s;
        XMStoreFloat3(&s, ownerScale);
        const auto safeDiv = [](float v, float d) { return (d > 1e-6f) ? v / d : v; };

        t.parent   = msg.owner;
        t.position = XMFLOAT3(safeDiv(msg.offset.x, s.x), safeDiv(msg.offset.y, s.y), safeDiv(msg.offset.z, s.z));
        t.isDirty  = true;
        t.UpdateMatrix(ownerWorld);
    }
    else
    {
        // 親を持たないのでローカル座標がそのままワールド座標になる
        t.position = spawnPosF;
        t.rotation = ownerRot; // 球なので回転は効かないが、向きをそろえておく
        t.isDirty  = true;
        t.UpdateMatrix();
    }
    ref.Set(t);

    // 作った時から判定を開いておく。ownerResolved を true にして、CombatAnimationSyncSystem に触らせない
    // 追従する判定は親をたどると持ち主の Animator が見つかるので、探させると HitBox 区間がないとして閉じられる
    HitboxComponent hb{};
    hb.ownerID         = msg.owner;
    hb.damageAmount    = msg.damage;
    hb.knockback       = msg.knockback;
    hb.isActive        = true;
    hb.hitStopDuration = msg.hitStopDuration;
    hb.hitStopScale    = msg.hitStopScale;
    hb.ownerResolved   = true;
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

    if (msg.follow)
    {
        // 生成したときのシーケンスを覚えておき、Update で持ち主の今のシーケンスと比べる
        const auto *animator = m_world->GetComponent<AnimatorComponent>(msg.owner);

        HitboxLifeAnimComponent follow{};
        follow.owner    = msg.owner;
        follow.sequence = animator ? animator->currentSequence : nullptr;
        ref.Set(follow);
    }

    CCL_LOG_INFO(LogCategory::Game,
                 "[SpawnHitbox] owner=%llu pos=(%.2f,%.2f,%.2f) r=%.2f dmg=%.1f life=%.2fs layer=%d follow=%d",
                 msg.owner, spawnPosF.x, spawnPosF.y, spawnPosF.z,
                 msg.radius, msg.damage, life.remainingLife, static_cast<int>(layer),
                 msg.follow ? 1 : 0);
}



// 作るのはイベントで行うので、ステージは寿命を減らすタイミングだけに関わる
// 当たり判定と同じ L05_Collision に置く
REGISTER_LOGIC_SYSTEM(SpawnHitboxSystem, Priority::LogicStage::L05_Collision);
