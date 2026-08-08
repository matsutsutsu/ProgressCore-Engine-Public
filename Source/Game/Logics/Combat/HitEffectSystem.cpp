/// @file HitEffectSystem.cpp
/// 役割・設計方針は HitEffectSystem.h を参照。
#include "HitEffectSystem.h"

#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

#include "Engine/Graphics/Systems/Particle/VfxSystem.h"
#include "Game/Logics/GameEvent/GameEvents.h"

using namespace CCL::ECS;

namespace
{
// スパークのバーストエミッタ・プレハブ（.json は Prefab 側が補完する）。
const char *kHitSparkPrefab = "Assets/Prefabs/Particles/VFX_HitSpark";
} // namespace

void HitEffectSystem::Initialize()
{
    // 確定ヒットの通知を受けて、その位置にスパークのバーストを1個出す。
    //   プールからの貸し出し・姿勢の確定・バースト発火・寿命後の返却は VfxSystem が
    //   引き受けるので、ここは「どこに出すか」だけを書けばよい。
    ListenEvent<HitImpactEvent>(
        [this](const HitImpactEvent &e) { Vfx::Play(*m_world, kHitSparkPrefab, e.position); });
}

void HitEffectSystem::Update(float /*dt*/)
{
    // 何もしない。再生中インスタンスの寿命管理は VfxSystem(L08) が持つ。
    //   購読のためだけに System として存在している。
}

// ダメージ解決(L06)後の後片付け相当のタイミングに置く（実処理はイベント駆動）。
REGISTER_LOGIC_SYSTEM(HitEffectSystem, Priority::LogicStage::L07_Cleanup);
