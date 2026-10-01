#include "VFXSystem.h"

#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include "Engine/Serialization/Factory/Prefab.h"
#include "Engine/Transform/Systems/TransformUpdateSystem.h"
#include "Engine/Platform/Core/Logger.h"
#include "Engine/Animation/Core/AnimEventMessages.h" // アニメーションの Effect イベント

#include <algorithm>

using namespace CCL::ECS;
using namespace DirectX;

// =========================================================================
void VFXSystem::Initialize()
{
    // VFX::Play から自分を引けるようにする。
    // World が消えるとこの登録も消えるので、シーンを切り替えても古いポインタは残らない。
    m_world->AddResource<VFXSystem *>(this);

    // アニメーションの Effect イベント（AnimationSystem が L02 で Publish する）
    ListenEvent<AnimEventEffectMessage>([this](const AnimEventEffectMessage &msg) { OnAnimEffect(msg); });
}

// =========================================================================
void VFXSystem::OnAnimEffect(const AnimEventEffectMessage &msg)
{
    if (msg.effectPath.empty()) return;

    // 1. 持ち主の姿勢。無ければ位置が決まらないので出さない
    const auto *ownerT = m_world->GetComponent<TransformComponent>(msg.entity);
    if (!ownerT) return;

    // 2. offset を持ち主の回転で回して、出す位置（ワールド座標）を求める（SpawnHitboxSystem と同じ規則）
    const XMFLOAT3 ownerPos = ownerT->GetWorldPosition();
    const XMFLOAT4 ownerRot = ownerT->GetWorldRotation();

    const XMVECTOR off = XMVector3Rotate(XMVectorSet(msg.offset.x, msg.offset.y, msg.offset.z, 0.0f),
                                         XMLoadFloat4(&ownerRot));
    XMFLOAT3       spawnPos;
    XMStoreFloat3(&spawnPos, XMVectorAdd(XMLoadFloat3(&ownerPos), off));

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
        // 持ち主の子にする。子の position には持ち主の scale が掛かる（Muriel は 5 倍）ので、
        //   offset を持ち主の scale で割り、追従しないときと同じく「ワールドのメートル」にそろえる
        const XMMATRIX ownerWorld = XMLoadFloat4x4(&ownerT->worldMatrix);
        XMVECTOR       ownerScale, ownerRotQ, ownerTrans;
        XMMatrixDecompose(&ownerScale, &ownerRotQ, &ownerTrans, ownerWorld);
        XMFLOAT3 s;
        XMStoreFloat3(&s, ownerScale);
        const auto safeDiv = [](float v, float d) { return (d > 1e-6f) ? v / d : v; };

        TransformUpdateSystem::SetParent(*m_world, id, msg.entity);
        transform->position = XMFLOAT3(safeDiv(msg.offset.x, s.x), safeDiv(msg.offset.y, s.y), safeDiv(msg.offset.z, s.z));
        transform->rotation = XMFLOAT4(0.0f, 0.0f, 0.0f, 1.0f); // 向きは持ち主に任せる
        transform->isDirty  = true;
        transform->UpdateMatrix(ownerWorld);
    }
    else
    {
        // 出した場所に残す。向きだけ持ち主にそろえる（前に飛ぶ・扇状に広がる VFX が正面を向くように）
        transform->rotation = ownerRot;
        transform->isDirty  = true;
        transform->UpdateMatrix();
    }

    // 6. 継続放出を含むなら、ブロック長だけ出してから止める。
    //    アニメのイベントから出した VFX には、Stop() を呼ぶ呼び出し側がいないため
    if (HasContinuousEmitter(*particle))
        m_timedStops.push_back({id, (std::max)(msg.duration, kMinEmitDuration)});
}

// =========================================================================
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

// =========================================================================
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

// =========================================================================
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
    //    エディタで保存し直したプレハブには親の ID が入っていることがあるので、切り離しておく。
    TransformUpdateSystem::DetachChild(*m_world, id);
    transform->position = worldPos;
    transform->isDirty  = true;

    // 3. ワールド行列をここで計算する
    //    パーティクルの発生位置はワールド行列から読むが、TransformUpdateSystem が計算するのは
    //    次のフレーム。待つと最初の1フレームだけ原点で粒子が出るので、ここで1回計算しておく。
    transform->UpdateMatrix();

    // 4. 全エミッタのバーストを起こす
    particle->hasPrevTransform = false; // 前フレームの位置を今の位置で初期化させる
    particle->TriggerBurst();

    // 5. 破棄を予約する
    //    継続放出はいつ終わるかがデータから決まらないので予約しない。
    //    呼び出し側が Stop() を呼ぶまで出し続ける（ループする VFX が勝手に消えないように）。
    if (!HasContinuousEmitter(*particle))
        m_active.push_back({id, CalcParticleLifetime(*particle)});

    return id;
}

// =========================================================================
void VFXSystem::Stop(EntityID handle)
{
    if (handle == InvalidEntityID || !m_world->IsEntityValid(handle)) return;

    // 既に破棄待ちなら触らない。毎フレーム Stop を呼ばれても寿命が延び続けないように。
    if (IsPendingDestroy(handle)) return;

    auto *particle = m_world->GetComponent<GPUParticleComponent>(handle);
    if (!particle)
    {
        // パーティクルでないものが渡された。待つ理由が無いのですぐ破棄する
        TransformUpdateSystem::MarkForDestruction(*m_world, handle);
        return;
    }

    // 発生だけを止める（GPU へ emit_enabled=0 として渡る）。
    //   出ている粒子は寿命まで残る。すぐに破棄すると粒子が途中で途切れる。
    particle->SetEmitting(false);

    // 最後の粒子が消えたころに破棄する
    m_active.push_back({handle, CalcParticleLifetime(*particle)});
}

// =========================================================================
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

        // 残り時間はその Entity の時間（localDt）で減らす。
        //   粒子は GPUParticleUpdateSystem が localDt で動かしているので、ここだけ実時間で数えると、
        //   ヒットストップやジャスト回避のスロー中に、まだ飛んでいる粒子を消してしまう
        //   （0.3 倍のスロー中なら、寿命の3割しか進んでいない時点で破棄される）。
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

// =========================================================================
bool VFXSystem::HasContinuousEmitter(const GPUParticleComponent &particle) const
{
    for (const auto &emitter : particle.emitters)
        if (emitter.desc.emitMode == 0) return true; // 0:継続 1:バースト
    return false;
}

// =========================================================================
float VFXSystem::CalcParticleLifetime(const GPUParticleComponent &particle) const
{
    // 一番長く残る粒子が消えるまでの時間を探す
    float longest = 0.0f;

    for (const auto &emitter : particle.emitters)
    {
        // spawnDelay（生まれるまでの待ち時間）は継続放出のときだけ効く
        //   （バーストは生成時に age=0 から始まり、遅れを使わない）。
        const float delay = (emitter.desc.emitMode == 1) ? 0.0f : emitter.desc.spawnDelay.y;
        const float life  = delay + emitter.desc.lifespan.y;
        if (life > longest) longest = life;
    }

    return longest + kLifetimeMargin;
}

// =========================================================================
bool VFXSystem::IsPendingDestroy(EntityID handle) const
{
    for (const auto &v : m_active)
        if (v.id == handle) return true;
    return false;
}

// =========================================================================
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
} // namespace VFX

REGISTER_LOGIC_SYSTEM(VFXSystem, Priority::LogicStage::L08_Prefab);
