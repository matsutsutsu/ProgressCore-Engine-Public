// ===========================================================================
// Systems/BulletTravelSystem.cpp
// ------------------------------
// @file
// @brief BulletTravelSystem の実体。Travel命令解釈と直進移動。
// ===========================================================================
#include "BulletTravelSystem.h"
#include "Game/Logics/Wand/Registry/WandProgramRegistry.h"
#include "Game/Logics/Wand/Data/CompiledWandProgram.h"
#include "Engine/Platform/Core/Logger.h"

#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

using namespace CCL::ECS;

namespace
{
    /**
     * @brief Travelフェーズの命令列を解釈し、弾の速度・向きを更新する。
     * @param dt このフレームの localDt（ヒットストップ追従済み）。
     * @note 現状は Accelerate のみ。他概念はこの switch に case を足すだけで拡張可能。
     */
    void ApplyTravelOps(const CompiledWandProgram& prog, BulletStateComponent& st, float dt)
    {
        for (uint8_t i = 0; i < prog.travelCount; ++i)
        {
            const EffectOp& op = prog.travelOps[i];
            switch (op.type)
            {
            case EffectType::Accelerate:
                // params[0] = 1秒あたりの加速率。毎フレーム speed *= (1 + rate*dt)（複利的に加速）。
                for (uint16_t s = 0; s < op.stackCount; ++s)
                    st.speed *= (1.0f + op.params[0] * dt);
                // params[1] = 旋回抵抗。Homing 実装時に「向きの変えにくさ」として使用（現状未使用）。
                break;

                // TODO(後工程): Slow(急減速→空中停止), Homing(最近敵へ向き補正), Explosion(間欠小爆発),
                //               Split(0.3秒後に左右分裂), Reflection(壁反射), Chaos(向き/速度急変)...
            default:
                break;
            }
        }
    }

} // anonymous namespace

void BulletTravelSystem::Update(float /*dt*/)
{
    if (!_world->HasResource<WandProgramRegistry>()) return;
    
    const WandProgramRegistry& registry = _world->GetResource<WandProgramRegistry>();

    // per-entity 書き込み + Registry読み取りのみ。構造変更しないので並列実行が安全。
    ForEachParallel([&registry](TransformComponent& t,
        BulletStateComponent& st,
        const BulletProgramRef& ref,
        const BulletTag&               /*tag*/,
        const TimeStateComponent& time)
        {
            const float dt = time.localDt;

            // 1. Travelフェーズ命令（速度・向きの変化）。ID不正時はスキップ（保険）。
            if (registry.IsValid(ref.programId))
            {
                const CompiledWandProgram& prog = registry.Get(ref.programId);
                ApplyTravelOps(prog, st, dt);
            }

            // 2. 直進移動（向き×速さ×localDt）。
            t.position = t.position + st.direction * st.speed * dt; // ※position のフィールド名は実装に合わせる
            t.isDirty = true;                          // ※dirty 契約がある場合のみ

            // 3. 寿命カウント。実際の破棄は L07 の BulletLifetimeSystem が行う。
            st.age += dt;
        });
}

REGISTER_LOGIC_SYSTEM(BulletTravelSystem, Priority::LogicStage::L02_Update);