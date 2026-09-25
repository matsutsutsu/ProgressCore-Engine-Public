#include "VFXSystem.h"

#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include "Engine/Serialization/Factory/Prefab.h"
#include "Engine/Transform/Systems/TransformUpdateSystem.h"
#include "Engine/Platform/Core/Logger.h"

using namespace CCL::ECS;

// =========================================================================
void VFXSystem::Initialize()
{
    // VFX::Play から自分を引けるようにする。
    // World が消えるとこの登録も消えるので、シーンを切り替えても古いポインタは残らない。
    m_world->AddResource<VFXSystem *>(this);
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
