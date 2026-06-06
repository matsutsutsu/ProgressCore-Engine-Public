// ===========================================================================
// @file   RF_WandBuildCompileSystem.cpp
// @brief  ビルド(WandBuildComponent)が変更されたら、コンパイル済みプログラムへ
//         変換し、その ID を杖(WandComponent)に書き戻すシステム。
//
// @note   実行ステージは L01_Input（最上流）。理由は2つ:
//           1) この後の L02 で走る並列システム（Travel）がレジストリを読むため、
//              レジストリ書き換え（コンパイル）はそれ以前に完了させる必要がある。
//           2) ビルド変更はショップ＝戦闘外でのみ起きる仕様なので、入力段で
//              拾って即コンパイルするのが自然。
// @warning 戦闘中（飛行弾がいる状態）での再コンパイルは ProgramID を無効化する
//          ため禁止。dirty を立てるのはショップ処理だけにすること。
// ===========================================================================

#include "Engine/ECS/System/CCL_IfSystem.h"        // @note IfSystem。実パスに調整。
#include "Engine/ECS/System/CCL_SystemRegistry.h"   // @note REGISTER_LOGIC_SYSTEM / Priority。実パスに調整。
#include "Engine/Platform/Core/Logger.h"

#include "RF_WandComponents.h"
#include "RF_WandProgram.h"

using namespace CCL::ECS;
using namespace CCL::ECS::Core;
using namespace RuneForge;


/**
 * @brief dirty な杖ビルドをコンパイルし、有効プログラムIDを杖へ反映する。
 */
class WandBuildCompileSystem
    : public IfSystem<WandBuildCompileSystem,
                      Write<WandBuildComponent>, Write<WandComponent>>
{
public:
    WandBuildCompileSystem() : IfSystem("WandBuildCompileSystem") {}

    void Update(float /*dt*/) override
    {
        // レジストリ未登録なら何もしない（起動時に AddResource 必須）。
        if (!_world->HasResource<WandProgramRegistry>()) return;
        WandProgramRegistry& reg = _world->GetResource<WandProgramRegistry>();

        // 構造変更を伴わない純粋な書き換えのみ。シングルスレッドで十分（低頻度）。
        ForEach([&](WandBuildComponent& build, WandComponent& wand)
        {
            if (!build.dirty) return;

            wand.activeProgramId = reg.Compile(build);
            build.dirty = false;

            CCL_LOG_SUCCESS(LogCategory::Game,
                "Wand build compiled. programId=%u (registry size=%zu)",
                wand.activeProgramId, reg.Size());
        });
    }
};

// cpp 末尾で自動登録（規約）。
REGISTER_LOGIC_SYSTEM(WandBuildCompileSystem, Priority::LogicStage::L01_Input);
