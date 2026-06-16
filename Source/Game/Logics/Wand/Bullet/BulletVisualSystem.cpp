// ===========================================================================
// BulletVisualSystem.cpp
// ----------------------
// 役割・責務・設計方針はヘッダを参照。
//
// 実装の意図(Why):
//   - SpawnParticle は Entity生成・親子付けという構造変更を伴うため、ForEach 反復中に
//     直接呼ばず、対象(bulletId, programId)を一旦集めてから後処理で接ぐ。
//   - visualAttached は接ぐ前に true にする。VFXパスが空・無効でも再走査対象にしない
//     ため(毎フレーム同じ弾を舐め続けるのを防ぐ)。
// ===========================================================================
#include "Game/Logics/Wand/Bullet/BulletVisualSystem.h"
#include "Game/Logics/Wand/Compiler/WandProgramRegistry.h"
#include "Game/Logics/Wand/Compiler/CompiledWandProgram.h"
#include "Engine/Serialization/Factory/Prefab.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

#include <DirectXMath.h>
#include <vector>
#include <cstring>

using namespace CCL::ECS;


void BulletVisualSystem::Update(float /*dt*/)
{
    if (!_world->HasResource<WandProgramRegistry>()) return;
    const WandProgramRegistry& registry = _world->GetResource<WandProgramRegistry>();

    // 反復中の構造変更を避けるため、見た目未付与の弾を (id, programId) で集める。
    struct Pending { EntityID bullet; ProgramID program; };
    std::vector<Pending> pendings;

    ForEachWithID([&](EntityID id,
        BulletStateComponent& st,
        const BulletProgramRef& ref,
        const BulletTag& /*tag*/)
        {
            if (st.visualAttached) return;
            st.visualAttached = true;          // 先に立てる(空/無効VFXでも再走査しない)
            pendings.push_back({ id, ref.programId });
        });

    // 集め終えてから接ぐ(ここで SpawnParticle=構造変更が走る)。
    for (const Pending& p : pendings)
    {
        if (!registry.IsValid(p.program)) continue;
        const CompiledWandProgram& prog = registry.Get(p.program);
        if (prog.bulletVfxPath[0] == '\0') continue; // VFX未指定の弾は見た目なし

        Prefab::SpawnParticle(*_world, prog.bulletVfxPath, p.bullet,
            DirectX::XMFLOAT3{ 0.0f, 0.0f, 0.0f }); // 弾の中心から発生
    }
}

REGISTER_LOGIC_SYSTEM(BulletVisualSystem, Priority::LogicStage::L02_PostUpdate);