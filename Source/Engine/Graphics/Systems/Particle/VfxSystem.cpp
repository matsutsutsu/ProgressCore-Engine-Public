// ===========================================================================
// VFXSystem.cpp
// 役割・前提はヘッダを参照。
// ===========================================================================
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
    // World が消えればこの登録も一緒に消えるので、シーンを切り替えても
    // 古いポインタが残らない。
    m_world->AddResource<VFXSystem *>(this);
}

// =========================================================================
EntityID VFXSystem::Play(const std::string &path, const DirectX::XMFLOAT3 &worldPos)
{
    // ① プレハブ(JSON)から1エンティティを作る
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

    // ② 指定位置へ置く
    //    プレハブをエディタで保存し直すと親IDが焼き込まれることがあるので、念のため切る。
    TransformUpdateSystem::DetachChild(*m_world, id);
    transform->position = worldPos;
    transform->isDirty  = true;

    // ③ ワールド行列をこの場で計算しておく
    //    パーティクルの発生位置は worldMatrix から読まれるが、その計算は
    //    TransformUpdateSystem が次のフレームに行う。待つと最初の1フレームだけ
    //    原点で粒子が出てしまうので、ここで1回だけ自分で計算する。
    transform->UpdateMatrix();

    // ④ 全エミッタのバーストを発火する
    particle->hasPrevTransform = false; // 前フレームの位置を今の位置で初期化させる
    particle->TriggerBurst();

    // ⑤ 後始末の予約。
    //    継続放出は「いつ終わるか」がデータから決まらないので、ここでは予約しない。
    //    呼び出し側が Stop() を呼ぶまで出続ける（ループVFXが勝手に消えないように）。
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
        // パーティクルでないものが渡された。待つ理由が無いのでそのまま破棄する。
        TransformUpdateSystem::MarkForDestruction(*m_world, handle);
        return;
    }

    // 発生だけ止める（GPU へ emit_enabled=0 として渡る）。
    //   既にいる粒子は寿命まで生きて自然に消える。即座に破棄すると
    //   粒子がブツッと途切れて不自然になるため。
    particle->SetEmitting(false);

    // 最後の粒子が消えたころに破棄する。
    m_active.push_back({handle, CalcParticleLifetime(*particle)});
}

// =========================================================================
void VFXSystem::Update(float dt)
{
    for (size_t i = 0; i < m_active.size();)
    {
        const EntityID id = m_active[i].id;

        // 他所から先に消されていたら、待つ意味がないので予約を捨てる。
        if (!m_world->IsEntityValid(id))
        {
            m_active[i] = m_active.back();
            m_active.pop_back();
            continue;
        }

        // 秒読みは「そのエンティティの時計」で進める。
        //   粒子の見た目は GPUParticleUpdateSystem が localDt で動かしているので、
        //   ここだけ実時間で数えると、ヒットストップやジャスト回避のスロー中に
        //   粒子がまだ飛んでいるのに寿命切れと判定して消してしまう。
        //   （0.3倍のスロー中なら、粒子の人生の3割しか進んでいない時点で破棄される）
        float step = dt;
        if (const auto *time = m_world->GetComponent<TimeStateComponent>(id))
            step = time->localDt;

        m_active[i].remain -= step;

        if (m_active[i].remain > 0.0f)
        {
            ++i;
            continue;
        }

        // 破棄する。実際に消すのは HierarchyCleanupSystem(L07)。
        TransformUpdateSystem::MarkForDestruction(*m_world, id);

        // 末尾を持ってきて詰める(順番はどうでもいいので これが一番速い)。
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
    // 一番長生きする粒子が消えるまでの時間を探す。
    float longest = 0.0f;

    for (const auto &emitter : particle.emitters)
    {
        // spawnDelay は「生まれるまでの待ち時間」。継続放出のときだけ効く
        //   （バーストは spawn 時に age=0 で始まり、遅延を使わない）。
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
