// ===========================================================================
// VfxSystem.cpp
// 役割・前提はヘッダを参照。
// ===========================================================================
#include "VfxSystem.h"

#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include "Engine/Serialization/Factory/Prefab.h"
#include "Engine/Transform/Systems/TransformUpdateSystem.h"
#include "Engine/Platform/Core/Logger.h"

using namespace CCL::ECS;

// =========================================================================
void VfxSystem::Initialize()
{
    m_active.clear();

    // Vfx::Play から自分を引けるようにする。
    // World が消えればこの登録も一緒に消えるので、シーンを切り替えても
    // 古いポインタが残らない。
    m_world->AddResource<VfxSystem *>(this);
}

// =========================================================================
EntityID VfxSystem::Play(const std::string &path, const DirectX::XMFLOAT3 &worldPos)
{
    // ① プレハブ(JSON)から1エンティティを作る
    EntityID id = Prefab::SpawnPrefab(*m_world, path);
    if (id == InvalidEntityID) return InvalidEntityID;

    auto *transform = m_world->GetComponent<TransformComponent>(id);
    auto *particle  = m_world->GetComponent<GPUParticleComponent>(id);
    if (!transform || !particle)
    {
        CCL_LOG_ERROR(LogCategory::Core,
                      "[Vfx] %s に Transform か GPUParticle がありません", path.c_str());
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

    // ⑤ 寿命を登録する。全エミッタの中で一番長生きする粒子が消えるまで待つ。
    float lifespan = 0.0f;
    for (const auto &emitter : particle->emitters)
        if (emitter.desc.lifespan.y > lifespan) lifespan = emitter.desc.lifespan.y;

    m_active.push_back({id, lifespan + kLifetimeMargin});

    return id;
}

// =========================================================================
void VfxSystem::Update(float dt)
{
    for (size_t i = 0; i < m_active.size();)
    {
        m_active[i].remain -= dt;

        if (m_active[i].remain > 0.0f)
        {
            ++i;
            continue;
        }

        // 破棄する。実際に消すのは HierarchyCleanupSystem(L07)。
        TransformUpdateSystem::MarkForDestruction(*m_world, m_active[i].id);

        // 末尾を持ってきて詰める(順番はどうでもいいので これが一番速い)。
        m_active[i] = m_active.back();
        m_active.pop_back();
    }
}

// =========================================================================
namespace Vfx
{
EntityID Play(World &world, const std::string &path, const DirectX::XMFLOAT3 &worldPos)
{
    if (!world.HasResource<VfxSystem *>())
    {
        CCL_LOG_ERROR(LogCategory::Core, "[Vfx] VfxSystem が登録されていません");
        return InvalidEntityID;
    }
    return world.GetResource<VfxSystem *>()->Play(path, worldPos);
}
} // namespace Vfx

REGISTER_LOGIC_SYSTEM(VfxSystem, Priority::LogicStage::L08_Prefab);
