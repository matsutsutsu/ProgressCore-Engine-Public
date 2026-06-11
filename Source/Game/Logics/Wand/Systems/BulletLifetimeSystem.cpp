// ===========================================================================
// Systems/BulletLifetimeSystem.cpp
// --------------------------------
// @file
// @brief BulletLifetimeSystem の実体。寿命切れの弾を破棄する。
// ===========================================================================
#include "BulletLifetimeSystem.h"
#include "Game/Logics/Wand/Registry/WandProgramRegistry.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h" // REGISTER_LOGIC_SYSTEM ※パス確認


using namespace CCL::ECS;

void BulletLifetimeSystem::Update(float /*dt*/)
{
    // 構造変更(破棄)は共有 PendingOps に積むため直列で回す（ForEachWithID は直列）。
    ForEachWithID([this](EntityID id,
        const BulletStateComponent& st,
        const BulletTag&            /*tag*/)
        {
            if (st.age >= st.lifetime)
            {
                // 安全破棄（DestroyTag付与 → 実破棄は内部で確定）。
                // ※メソッド名が異なる場合は _world->AddComponent<DestroyTag>(id, {}) でも可。
                _world->Destroy(id);
            }
        });
}

REGISTER_LOGIC_SYSTEM(BulletLifetimeSystem, Priority::LogicStage::L07_Cleanup);