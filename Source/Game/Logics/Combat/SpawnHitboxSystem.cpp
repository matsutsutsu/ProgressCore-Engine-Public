// ===========================================================================
// SpawnHitboxSystem.cpp
// 役割・責務・パイプライン上の位置付けは SpawnHitboxSystem.h を参照。
//
// 実装の意図(Why):
//   - 生成位置は owner の worldMatrix を基準にする。ただし生成直後の Transform は
//     worldMatrix が未計算(単位行列)で、JoltBodySetupSystem(L03) は GetWorldPosition()
//     を読むため、そのままだと剛体が原点に湧く。親を持たない判定球なので、生成時に
//     UpdateMatrix() で worldMatrix を即ベイクして正しい位置で剛体化させる。
//   - motionType=Kinematic + isSensor=true は既存の弾と同じ設定。すり抜けつつ
//     衝突コールバックだけを取り、HitboxCollisionSystem に判定を委ねる。
//   - 生成した Hitbox は ownerEntity=Invalid のまま。これにより
//     CombatAnimationSyncSystem の主役走査で「駆動元なし＝常時判定の弾」として
//     スキップされ、区間ポーリングの影響を受けない。
// ===========================================================================
#include "SpawnHitboxSystem.h"

#include "Engine/Animation/Core/AnimEventMessages.h"
#include "CombatComponents.h"  // HitboxComponent
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
// 区間長ゼロ(点イベント)で来たときの最小生存時間。
// FixedUpdate(60Hz) で数フレーム＝センサーが確実に1回は重なりを検出できる長さ。
constexpr float kMinHitboxLifetime = 0.05f;
} // namespace

void SpawnHitboxSystem::Initialize()
{
    // 生成依頼は AnimationSystem(L02_PostUpdate) が同期 Publish する。
    // 本コールバックはその Publish 内で実行され、Spawn は遅延構造変更として積まれる。
    ListenEvent<AnimEventSpawnHitboxMessage>([this](const AnimEventSpawnHitboxMessage& msg) {
        OnSpawnHitbox(msg);
    });

    CCL_LOG_INFO(LogCategory::Game, "SpawnHitboxSystem Initialized.");
}

void SpawnHitboxSystem::Update(float dt)
{
    // 生成した判定球の寿命を減算し、尽きたら自身を破棄する。
    // 破棄は遅延処理(次の ScrutinyAndApply で反映)。剛体は JoltCleanupSystem が回収。
    // ※生成そのものは ListenEvent コールバック(OnSpawnHitbox)側で行う。
    ForEachWithID([&](CCL::ECS::EntityID id, HitboxLifetimeComponent& life) {
        life.remainingLife -= dt;
        if (life.remainingLife <= 0.0f)
            _world->Destroy(id);
    });
}

void SpawnHitboxSystem::OnSpawnHitbox(const AnimEventSpawnHitboxMessage& msg)
{
    // ── 1. 生成基準(owner)の Transform。無ければ位置が定まらないので生成しない ──
    const auto* ownerT = _world->GetComponent<TransformComponent>(msg.owner);
    if (!ownerT)
    {
        CCL_LOG_WARN(LogCategory::Game,
            "[SpawnHitbox] owner %llu に Transform 無し。生成中止", msg.owner);
        return;
    }

    // ── 2. offset を owner のワールド回転で回し、ワールド位置を算出(前方=キャラの向き) ──
    const XMFLOAT3 ownerPos = ownerT->GetWorldPosition();
    const XMFLOAT4 ownerRot = ownerT->GetWorldRotation();

    const XMVECTOR off        = XMVectorSet(msg.offset.x, msg.offset.y, msg.offset.z, 0.0f);
    const XMVECTOR rotatedOff = XMVector3Rotate(off, XMLoadFloat4(&ownerRot));
    const XMVECTOR spawnPos   = XMVectorAdd(XMLoadFloat3(&ownerPos), rotatedOff);

    XMFLOAT3 spawnPosF;
    XMStoreFloat3(&spawnPosF, spawnPos);

    // ── 3. 衝突レイヤーはイベントで明示指定（＝誰に当てるか） ──
    //   PLAYER_ATTACK(3) ならボスに命中、BOSS_ATTACK(4) ならプレイヤーに命中。
    //   Sequencer の SpawnHitbox 設定で選ぶ。陣営(team)には依存しない。
    const JPH::ObjectLayer layer = static_cast<JPH::ObjectLayer>(msg.attackLayer);

    // ── 4. 判定球エンティティを生成 ──
    static const Archetype hitboxArch = ArchetypeHelper::Generate<
        TransformComponent, HitboxComponent, JoltRigidbodyComponent,
        JoltSphereColliderComponent, HitboxLifetimeComponent>();
    auto ref = _world->Spawn(hitboxArch);

    // Transform: 親を持たないため local=world。生成直後に worldMatrix をベイクして
    // JoltBodySetupSystem(L03) が正しい位置で剛体を作れるようにする(原点湧き防止)。
    TransformComponent t{};
    t.position = spawnPosF;
    t.rotation = ownerRot; // 球は回転無関係だが、向きを揃えておく
    t.isDirty  = true;
    t.UpdateMatrix();      // ★ worldMatrix 即時ベイク
    ref.Set(t);

    // Hitbox: 生成時から判定ON。ownerEntity=Invalid のまま＝常時判定の弾扱い。
    HitboxComponent hb{};
    hb.ownerID         = msg.owner;
    hb.damageAmount    = msg.damage;
    hb.knockback       = msg.knockback;
    hb.isActive        = true;
    hb.hitStopDuration = msg.hitStopDuration;
    hb.hitStopScale    = msg.hitStopScale;
    ref.Set(hb);

    // Rigidbody: 動かないが sensor 判定を取るため Kinematic + Sensor(既存の弾と同設定)。
    JoltRigidbodyComponent rb{};
    rb.motionType    = JPH::EMotionType::Kinematic;
    rb.objectLayer   = layer;
    rb.isSensor      = true;
    rb.gravityFactor = 0.0f;
    ref.Set(rb);

    // Sphere: イベントの半径をそのまま形状に反映。
    JoltSphereColliderComponent sc{};
    sc.radius = msg.radius;
    ref.Set(sc);

    // Lifetime: 区間長ぶん生存させ、尽きたら本システムの Update が Destroy する
    //           (剛体は JoltCleanupSystem が回収)。
    HitboxLifetimeComponent life{};
    life.remainingLife = (msg.lifeTime > 0.0f) ? msg.lifeTime : kMinHitboxLifetime;
    ref.Set(life);

    CCL_LOG_INFO(LogCategory::Game,
        "[SpawnHitbox] owner=%llu pos=(%.2f,%.2f,%.2f) r=%.2f dmg=%.1f life=%.2fs layer=%d",
        msg.owner, spawnPosF.x, spawnPosF.y, spawnPosF.z,
        msg.radius, msg.damage, life.remainingLife, static_cast<int>(layer));
}

// イベント駆動のため Update ステージは実質不問。命中判定(HitboxCollisionSystem)と
// 同じ L05_Collision に置き、戦闘系をまとめる。
REGISTER_LOGIC_SYSTEM(SpawnHitboxSystem, Priority::LogicStage::L05_Collision);
