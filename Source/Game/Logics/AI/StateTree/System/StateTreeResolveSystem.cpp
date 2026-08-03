/// @file  StateTreeResolveSystem.cpp
#include "StateTreeResolveSystem.h"

#include "Game/Logics/AI/StateTree/Core/StateTreeRegistry.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include "Engine/Platform/Core/Logger.h"

using namespace CCL::ECS;
using namespace CCL::AI;

StateTreeResolveSystem::StateTreeResolveSystem()
    : IfSystem("StateTreeResolveSystem") {
}

void StateTreeResolveSystem::Update(float /*dt*/)
{
    // Registry がなければ何もできない
    if (!m_world->HasResource<StateTreeRegistry>()) return;
    auto& reg = m_world->GetResource<StateTreeRegistry>();

    // assetHandle が未解決かつ assetName がある Entity だけ処理
    ForEach([&](StateTreeComponent& st)
        {
            if (st.assetHandle.IsValid()) return;       // 既に解決済み
            if (st.assetName.empty())     return;       // 名前がない → どうしようもない

            const StateTreeHandle handle = reg.FindByName(st.assetName);
            if (handle.IsValid())
            {
                st.assetHandle = handle;
                CCL_LOG_INFO(LogCategory::Game,
                    "[StateTreeResolve] '%s' → handle=%u",
                    st.assetName.c_str(), handle.value);
            }
            // 見つからない場合はログを毎フレーム出すと洪水になるので出さない
            // （AIDebugSystem が STAGE-4 で検出してくれる）
        });
}

// L01_Input に置く理由:
//   StateTreeSystem (L02) より前に解決を終わらせるため。
REGISTER_LOGIC_SYSTEM(StateTreeResolveSystem, Priority::LogicStage::L01_Input)