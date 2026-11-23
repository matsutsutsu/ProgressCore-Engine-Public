#include "VFXSystem.h"

#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include "Engine/Serialization/Factory/Prefab.h"
#include "Engine/Transform/Systems/TransformUpdateSystem.h"
#include "Engine/Platform/Core/Logger.h"
#include "Engine/Animation/Core/AnimEventMessages.h"
#include "Engine/Animation/Components/CombatRosterComponent.h"
#include "Engine/Platform/Core/FileSystem.h"

#include <algorithm>

#include <tracy/Tracy.hpp>

using namespace CCL::ECS;
using namespace DirectX;

namespace
{
    // 先読みする VFX のプレハブの置き場
    constexpr const char *kVfxPrefabFolder = "Assets/Prefabs/Particles";
}

// ---------------------------------------------------------------------------
// 初期化
// ---------------------------------------------------------------------------

void VFXSystem::Initialize()
{
    // VFX::Play から自分を引けるようにする
    // World が消えるとこの登録も消えるので、シーンを切り替えても古いポインタは残らない
    m_world->AddResource<VFXSystem *>(this);

    // アニメーションの Effect イベント（AnimationSystem が L02 で Publish する）
    ListenEvent<AnimEventEffectMessage>([this](const AnimEventEffectMessage &msg) { OnAnimEffect(msg); });

    PreloadPrefabs();
}



void VFXSystem::PreloadPrefabs()
{
    ZoneScopedN("VFXSystem::PreloadPrefabs");

    // VFXフォルダの中を全部読む
    // 初回起動時の処理負荷を軽減させるために最初に読み込む
    for (const std::string &path : FileSystem::ListFiles(kVfxPrefabFolder, ".json"))
    {
        Prefab::Preload(path);
    }
}



// ---------------------------------------------------------------------------
// アニメのイベントから出すエフェクト
// ---------------------------------------------------------------------------

void VFXSystem::OnAnimEffect(const AnimEventEffectMessage &msg)
{
    if (msg.effectPath.empty()) return;

    // 1. 基準にする Entity と、その姿勢。姿勢がなければ位置が決まらないので出さない
    const EntityID anchor  = ResolveAnchor(msg);
    const auto    *anchorT = m_world->GetComponent<TransformComponent>(anchor);
    if (!anchorT) return;

    // 2. offset を基準の回転で回して、出す位置（ワールド座標）を求める（SpawnHitboxSystem と同じ規則）
    const XMFLOAT3 anchorPos = anchorT->GetWorldPosition();
    const XMFLOAT4 anchorRot = anchorT->GetWorldRotation();

    const XMVECTOR off = XMVector3Rotate(XMVectorSet(msg.offset.x, msg.offset.y, msg.offset.z, 0.0f),
                                         XMLoadFloat4(&anchorRot));
    XMFLOAT3       spawnPos;
    XMStoreFloat3(&spawnPos, XMVectorAdd(XMLoadFloat3(&anchorPos), off));

    // 3. 出す。Play が位置を置き、バーストを起こし、バーストだけなら破棄も予約する
    const EntityID id = Play(msg.effectPath, spawnPos);
    if (id == InvalidEntityID) return;

    auto *transform = m_world->GetComponent<TransformComponent>(id);
    auto *particle  = m_world->GetComponent<GPUParticleComponent>(id);

    // 4. 大きさ。バーストはこのフレームの更新で出るので、ここで設定を書き換えれば間に合う
    ApplyScale(*particle, msg.scale);

    // 5. 向きと追従
    if (msg.follow)
    {
        // 基準の子にする。子の position には基準の scale が掛かる（Valkyra は 5 倍）ので、
        //   offset を基準の scale で割り、追従しないときと同じく「ワールドのメートル」にそろえる
        const XMMATRIX anchorWorld = XMLoadFloat4x4(&anchorT->worldMatrix);
        XMVECTOR       anchorScale, anchorRotQ, anchorTrans;
        XMMatrixDecompose(&anchorScale, &anchorRotQ, &anchorTrans, anchorWorld);
        XMFLOAT3 s;
        XMStoreFloat3(&s, anchorScale);
        const auto safeDiv = [](float v, float d) { return (d > 1e-6f) ? v / d : v; };

        TransformUpdateSystem::SetParent(*m_world, id, anchor);
        transform->position = XMFLOAT3(safeDiv(msg.offset.x, s.x), safeDiv(msg.offset.y, s.y), safeDiv(msg.offset.z, s.z));
        transform->rotation = XMFLOAT4(0.0f, 0.0f, 0.0f, 1.0f); // 向きは基準に任せる
        transform->isDirty  = true;
        transform->UpdateMatrix(anchorWorld);
    }
    else
    {
        // 出した場所に残す。向きだけ基準にそろえる（前に飛ぶ・扇状に広がる VFX が正面を向くように）
        transform->rotation = anchorRot;
        transform->isDirty  = true;
        transform->UpdateMatrix();
    }

    // 向きと位置を変えたので、描画補間の前ステップの値もそろえる（Play の説明と同じ理由）
    transform->prevPosition = transform->position;
    transform->prevRotation = transform->rotation;

    // 6. 継続放出を含むなら、ブロック長だけ出してから止める
    //    アニメのイベントから出した VFX には、Stop() を呼ぶ呼び出し側がいないため
    if (HasContinuousEmitter(*particle))
        m_timedStops.push_back({id, (std::max)(msg.duration, kMinEmitDuration)});
}



EntityID VFXSystem::ResolveAnchor(const AnimEventEffectMessage &msg) const
{
    // タグがなければキャラそのもの
    if (msg.attachTag.empty()) return msg.entity;

    // 持ち主の CombatRoster からタグの武器を引く。引けなければキャラで出す（VFX が消えるより気づきやすい）
    const auto    *roster = m_world->GetComponent<CombatRosterComponent>(msg.entity);
    const EntityID weapon = roster ? roster->Find(msg.attachTag.c_str()) : InvalidEntityID;
    if (weapon == InvalidEntityID)
    {
        CCL_LOG_WARN(LogCategory::Core, "[VFX] 武器タグ \"%s\" が CombatRoster に無いので、キャラの位置で出します",
                     msg.attachTag.c_str());
        return msg.entity;
    }
    return weapon;
}



void VFXSystem::ApplyScale(GPUParticleComponent &particle, float s) const
{
    if (s <= 0.0f || s == 1.0f) return;

    // 長さの次元を持つ値（m・m/秒・m/秒^2）にだけ掛ける。時間や個数・色は変えない
    const auto mul2 = [s](GpuData::float2 &v) { v.x *= s; v.y *= s; };
    const auto mul3 = [s](GpuData::float3 &v) { v.x *= s; v.y *= s; v.z *= s; };

    for (auto &emitter : particle.emitters)
    {
        ParticleEmitterDesc &d = emitter.desc;
        mul2(d.emitRadius);
        mul2(d.emitSize);
        mul2(d.emitSpeed);
        mul3(d.localOffset);
        d.localOffsetY *= s;
        mul3(d.lineEnd);
        d.gravity *= s;
        d.maxSpeed *= s;
        mul3(d.attractorPos);
        d.attractorStrength *= s;
        d.attractorRadius *= s;
    }
    particle.MarkDescChanged();
}



void VFXSystem::UpdateTimedStops(float dt)
{
    for (size_t i = 0; i < m_timedStops.size();)
    {
        const EntityID id = m_timedStops[i].id;

        // 先に消えていたら予約を捨てる
        bool done = !m_world->IsEntityValid(id);
        if (!done)
        {
            // 粒子と同じく、その Entity の時間（localDt）で数える（ヒットストップ中は進まない）
            const auto *time = m_world->GetComponent<TimeStateComponent>(id);
            m_timedStops[i].remain -= time ? time->localDt : dt;
            if (m_timedStops[i].remain <= 0.0f)
            {
                Stop(id); // 発生を止め、粒子が消えたころに破棄する
                done = true;
            }
        }

        if (!done)
        {
            ++i;
            continue;
        }
        m_timedStops[i] = m_timedStops.back();
        m_timedStops.pop_back();
    }
}



// ---------------------------------------------------------------------------
// 直接の再生と停止
// ---------------------------------------------------------------------------

EntityID VFXSystem::Play(const std::string &path, const DirectX::XMFLOAT3 &worldPos)
{
    // 1. プレハブ（JSON）から1 Entity を作る
    EntityID id = Prefab::SpawnPrefab(*m_world, path);
    if (id == InvalidEntityID) return InvalidEntityID;

    auto *transform = m_world->GetComponent<TransformComponent>(id);
    auto *particle  = m_world->GetComponent<GPUParticleComponent>(id);
    if (!transform || !particle)
    {
        CCL_LOG_ERROR(LogCategory::Core,
                      "[VFX] %s に Transform か GPUParticle がありません", path.c_str());
        TransformUpdateSystem::MarkForDestruction(*m_world, id);
        return InvalidEntityID;
    }

    // 2. 指定位置に置く
    //    エディタで保存し直したプレハブには親の ID が入っていることがあるので、切り離しておく
    TransformUpdateSystem::DetachChild(*m_world, id);
    transform->position = worldPos;
    transform->isDirty  = true;

    //    描画補間の前ステップの値もそろえる。そのままだと原点から今の位置へ補間されて見え、
    //    パーティクルは描画の位置（renderWorldMatrix）から出るので、生成位置が大きくずれる
    transform->prevPosition = worldPos;
    transform->prevRotation = transform->rotation;

    // 3. ワールド行列をここで計算する
    //    パーティクルの発生位置はワールド行列から読むが、TransformUpdateSystem が計算するのは
    //    次のフレーム。待つと最初の1フレームだけ原点で粒子が出るので、ここで1回計算しておく
    transform->UpdateMatrix();

    // 4. 全エミッタのバーストを起こす
    particle->hasPrevTransform = false; // 前フレームの位置を今の位置で初期化させる
    particle->TriggerBurst();

    // 5. 破棄を予約する
    //    継続放出はいつ終わるかがデータから決まらないので予約しない
    //    呼び出し側が Stop() を呼ぶまで出し続ける（ループする VFX が勝手に消えないように）
    if (!HasContinuousEmitter(*particle))
        m_active.push_back({id, CalcParticleLifetime(*particle)});

    return id;
}



void VFXSystem::Stop(EntityID handle)
{
    if (handle == InvalidEntityID || !m_world->IsEntityValid(handle)) return;

    // 既に破棄待ちなら触らない。毎フレーム Stop を呼ばれても寿命が延び続けないように
    if (IsPendingDestroy(handle)) return;

    auto *particle = m_world->GetComponent<GPUParticleComponent>(handle);
    if (!particle)
    {
        // パーティクルでないものが渡された。待つ理由がないのですぐ破棄する
        TransformUpdateSystem::MarkForDestruction(*m_world, handle);
        return;
    }

    // 発生だけを止める（GPU へ emit_enabled=0 として渡る）
    //   出ている粒子は寿命まで残る。すぐに破棄すると粒子が途中で途切れる
    particle->SetEmitting(false);

    // 最後の粒子が消えたころに破棄する
    m_active.push_back({handle, CalcParticleLifetime(*particle)});
}



void VFXSystem::Destroy(EntityID handle)
{
    // ID には世代が入っているので、寿命で消えた後の古い ID ならここで弾かれる
    if (handle == InvalidEntityID || !m_world->IsEntityValid(handle)) return;

    // 破棄の印を付ける。実際に消すのは HierarchyCleanupSystem（L07）
    // 破棄待ちの予約は、消えた後の Update が「他から先に消された」として捨てる
    TransformUpdateSystem::MarkForDestruction(*m_world, handle);
}



// ---------------------------------------------------------------------------
// 破棄までの見届け
// ---------------------------------------------------------------------------

void VFXSystem::Update(float dt)
{
    // 先に止める。止めたものは m_active に入り、下のループで破棄まで数える
    UpdateTimedStops(dt);

    for (size_t i = 0; i < m_active.size();)
    {
        const EntityID id = m_active[i].id;

        // 他から先に消されていたら、予約を捨てる
        if (!m_world->IsEntityValid(id))
        {
            m_active[i] = m_active.back();
            m_active.pop_back();
            continue;
        }

        // 残り時間はその Entity の時間（localDt）で減らす
        //   粒子は GPUParticleUpdateSystem が localDt で動かしているので、ここだけ実時間で数えると、
        //   ヒットストップやジャスト回避のスロー中に、まだ飛んでいる粒子を消してしまう
        //   （0.3 倍のスロー中なら、寿命の3割しか進んでいない時点で破棄される）
        float step = dt;
        if (const auto *time = m_world->GetComponent<TimeStateComponent>(id))
            step = time->localDt;

        m_active[i].remain -= step;

        if (m_active[i].remain > 0.0f)
        {
            ++i;
            continue;
        }

        // 破棄の印を付ける。実際に消すのは HierarchyCleanupSystem（L07）
        TransformUpdateSystem::MarkForDestruction(*m_world, id);

        // 順番は問わないので、末尾の要素を持ってきて詰める
        m_active[i] = m_active.back();
        m_active.pop_back();
    }
}



bool VFXSystem::HasContinuousEmitter(const GPUParticleComponent &particle) const
{
    for (const auto &emitter : particle.emitters)
        if (emitter.desc.emitMode == 0) return true; // 0:継続 1:バースト
    return false;
}



float VFXSystem::CalcParticleLifetime(const GPUParticleComponent &particle) const
{
    // 一番長く残る粒子が消えるまでの時間を探す
    float longest = 0.0f;

    for (const auto &emitter : particle.emitters)
    {
        // spawnDelay（生まれるまでの待ち時間）は継続放出のときだけ効く
        //   （バーストは生成時に age=0 から始まり、遅れを使わない）
        const float delay = (emitter.desc.emitMode == 1) ? 0.0f : emitter.desc.spawnDelay.y;
        const float life  = delay + emitter.desc.lifespan.y;
        if (life > longest) longest = life;
    }

    return longest + kLifetimeMargin;
}



bool VFXSystem::IsPendingDestroy(EntityID handle) const
{
    for (const auto &v : m_active)
        if (v.id == handle) return true;
    return false;
}



// ---------------------------------------------------------------------------
// どこからでも呼べる窓口（VFX::Play / Stop）
// ---------------------------------------------------------------------------

namespace VFX
{
    EntityID Play(World &world, const std::string &path, const DirectX::XMFLOAT3 &worldPos)
    {
        if (!world.HasResource<VFXSystem *>())
        {
            CCL_LOG_ERROR(LogCategory::Core, "[VFX] VFXSystem が登録されていません");
            return InvalidEntityID;
        }
        return world.GetResource<VFXSystem *>()->Play(path, worldPos);
    }



    void Stop(World &world, EntityID handle)
    {
        if (!world.HasResource<VFXSystem *>())
        {
            CCL_LOG_ERROR(LogCategory::Core, "[VFX] VFXSystem が登録されていません");
            return;
        }
        world.GetResource<VFXSystem *>()->Stop(handle);
    }



    void Destroy(World &world, EntityID handle)
    {
        if (!world.HasResource<VFXSystem *>())
        {
            CCL_LOG_ERROR(LogCategory::Core, "[VFX] VFXSystem が登録されていません");
            return;
        }
        world.GetResource<VFXSystem *>()->Destroy(handle);
    }
}

REGISTER_LOGIC_SYSTEM(VFXSystem, Priority::LogicStage::L08_Prefab);
