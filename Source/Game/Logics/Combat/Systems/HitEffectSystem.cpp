#include "Game/Logics/Combat/Systems/HitEffectSystem.h"

#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

#include "Engine/Graphics/Systems/Particle/VFXSystem.h"
#include "Game/Core/GameEvents.h"

using namespace CCL::ECS;

namespace
{
    // 火花を一度に出すエミッタのプレハブ
    const char *kHitSparkPrefab = "Assets/Prefabs/Particles/VFX_HitSpark";
} // namespace

void HitEffectSystem::Initialize()
{
    // 当たったという通知を受けて、その位置に火花を1回出す。
    // 火花のエミッタはすべて Burst（一度に出して終わる）なので VFXSystem が自動で片付ける。戻り値は持たなくてよい
    ListenEvent<HitImpactEvent>(
        [this](const HitImpactEvent &e) { VFX::Play(*m_world, kHitSparkPrefab, e.position); });
}

void HitEffectSystem::Update(float /*dt*/)
{
    // 何もしない。イベントを購読するために System にしている
}

// 処理はイベントで動くので、ステージはダメージの処理（L06）の後であればよい
REGISTER_LOGIC_SYSTEM(HitEffectSystem, Priority::LogicStage::L07_Cleanup);
