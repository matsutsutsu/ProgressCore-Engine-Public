// ===========================================================================
// WandCompileSystem.cpp
// ---------------------
// @file
// @brief WandCompileSystem の実体。dirty なビルドをコンパイルして Registry に登録する。
// ===========================================================================
#include "WandCompileSystem.h"
#include "WandCompiler.h"
#include "WandProgramRegistry.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h" // REGISTER_LOGIC_SYSTEM ※パス確認
#include "Engine/Platform/Core/Logger.h"

using namespace CCL::ECS;

void WandCompileSystem::Update(float /*dt*/)
{
    // Resource 未登録なら何もしない（World 初期化時に AddResource<WandProgramRegistry>() が必須）。
    if (!_world->HasResource<WandProgramRegistry>())
    {
        //CCL_LOG_ERROR(LogCategory::Game,"WandProgramRegistry が未登録です。World 初期化で AddResource してください。");
        return;
    }

    WandProgramRegistry& registry = _world->GetResource<WandProgramRegistry>();

    // Resource(共有データ)を書き換えるため並列化しない。ForEach は直列実行。
    ForEach([&registry](WandBuildComponent& build)
        {
            if (!build.isDirty) return; // コンパイル不要

            const CompiledWandProgram prog = WandCompiler::Compile(build);
            build.cachedProgramId = registry.RegisterOrUpdate(build.cachedProgramId, prog);
            build.isDirty = false;

            CCL_LOG_INFO(LogCategory::Game,
                "杖ビルドをコンパイルしました (ProgramID=%u)", build.cachedProgramId);
        });
}

// ファイル末尾で自動登録（弾発射 L02 より前の L01 ステージで実行）
REGISTER_LOGIC_SYSTEM(WandCompileSystem, Priority::LogicStage::L01_Input);