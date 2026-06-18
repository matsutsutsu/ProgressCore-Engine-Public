// ===========================================================================
// Systems/BulletTravelSystem.cpp
// ------------------------------
// 役割:
//   飛行中の弾を毎フレーム更新する(Travelフェーズ)。Travel命令を TravelResolution へ
//   集約し、結果を弾の speed / direction へ適用してから直進移動・寿命加算を行う。
//
// 実装の意図(Why):
//   - Spawn と同じ「<Name>Travel ハンドラ → Resolution 集約 → 適用」の構造に揃える。
//   - Homing は敵探索(World/Resource依存)が要るためハンドラを純粋に保てない。そこで
//     「System が最寄り敵方向を確定 → HomingTravel に渡す」分担にする。探索は Homing を
//     含む弾だけ・1弾1回に限定し、TargetRegistry(平坦配列)を線形走査する。
//   - 探索コストを避けるため、travelOps に Homing が含まれるかを先に判定し、含む弾だけ
//     FindNearest する(直進弾は探索ゼロ)。
//
// 並列性:
//   TargetRegistry は読み取り専用参照。各弾の書き込みは per-entity のみ。構造変更なし。
//   よって ForEachParallel のまま安全(リストは L02_PreUpdate で構築済み)。
// ===========================================================================
#include "BulletTravelSystem.h"
#include "Game/Logics/Wand/Compiler/WandProgramRegistry.h"
#include "Game/Logics/Wand/Compiler/CompiledWandProgram.h"
#include "Game/Logics/Combat/TargetRegistry.h"
#include "Engine/Platform/Core/Logger.h"

#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

// Travel フェーズの解決結果とハンドラ群
#include "Game/Logics/Wand/Effects/TravelResolution.h"
#include "Game/Logics/Wand/Effects/Effect_Accelerate.h" // AccelerateTravel
#include "Game/Logics/Wand/Effects/Effect_Slow.h"       // SlowTravel
#include "Game/Logics/Wand/Effects/Effect_Homing.h"     // HomingTravel

using namespace CCL::ECS;
using DirectX::SimpleMath::Vector3;

namespace
{
    /// @brief travelOps に Homing が含まれるか（含む弾だけ敵探索する）。
    bool HasHoming(const CompiledWandProgram& prog)
    {
        for (uint8_t i = 0; i < prog.travelCount; ++i)
            if (prog.travelOps[i].type == EffectType::Homing) return true;
        return false;
    }

    /// @brief Travelフェーズ集約器。各 <Name>Travel が Resolution に積むだけ。
    /// @param targetDir  最寄り敵への方向（Homingが無い/敵が居ない場合は Zero）。
    /// @param hasTarget  有効な目標方向があるか。
    TravelResolution ResolveTravel(const CompiledWandProgram& prog, float dt,
        const Vector3& targetDir, bool hasTarget)
    {
        TravelResolution r;
        for (uint8_t i = 0; i < prog.travelCount; ++i)
        {
            const EffectOp& op = prog.travelOps[i];
            switch (op.type)
            {
            case EffectType::Accelerate: AccelerateTravel(op, r, dt); break;
            case EffectType::Slow:       SlowTravel(op, r, dt);       break;
            case EffectType::Homing:
                // 目標が無ければ追尾しない（敵不在 → 直進。Q3の仕様）。
                if (hasTarget) HomingTravel(op, targetDir, r, dt);
                break;
            default: break;
            }
        }
        return r;
    }

    /// @brief 解決結果を弾の状態へ適用する（speed倍率・向き補間）。
    void ApplyTravel(const TravelResolution& r, BulletStateComponent& st)
    {
        st.speed *= r.speedMult;

        if (r.hasSteering && r.targetDir.LengthSquared() > 1e-6f)
        {
            Vector3 dir = Vector3::Lerp(st.direction, r.targetDir, r.steerLerp);
            dir.y = 0.0f;
            if (dir.LengthSquared() > 1e-6f)
            {
                dir.Normalize();
                st.direction = dir;
            }
        }
    }
}

void BulletTravelSystem::Update(float /*dt*/)
{
    if (!_world->HasResource<WandProgramRegistry>()) return;
    const WandProgramRegistry& registry = _world->GetResource<WandProgramRegistry>();

    // ターゲットリスト（無ければ nullptr 扱い＝Homing無効）。読み取り専用。
    const TargetRegistry* targets =
        _world->HasResource<TargetRegistry>() ? &_world->GetResource<TargetRegistry>() : nullptr;

    ForEachParallel([&](TransformComponent& t,
        BulletStateComponent& st,
        const BulletProgramRef& ref,
        const BulletTag&               /*tag*/,
        const TimeStateComponent& time)
        {
            st.prevPosition = t.position; // 衝突判定の線分始点
            const float dt = time.localDt;

            if (registry.IsValid(ref.programId))
            {
                const CompiledWandProgram& prog = registry.Get(ref.programId);

                // Homing を含む弾だけ最寄り敵を探索（直進弾は探索ゼロ）。
                Vector3 targetDir = Vector3::Zero;
                bool    hasTarget = false;
                if (targets && !targets->Empty() && HasHoming(prog))
                {
                    Vector3 enemyPos;
                    const EntityID nearest =
                        targets->FindNearest(t.position, st.owner, enemyPos);
                    if (nearest != InvalidEntityID)
                    {
                        Vector3 d = enemyPos - t.position;
                        d.y = 0.0f;
                        if (d.LengthSquared() > 1e-6f)
                        {
                            d.Normalize();
                            targetDir = d;
                            hasTarget = true;
                        }
                    }
                }

                const TravelResolution r = ResolveTravel(prog, dt, targetDir, hasTarget);
                ApplyTravel(r, st);
            }

            // 直進移動。
            t.position = t.position + st.direction * st.speed * dt;
            t.isDirty = true;

            // 寿命カウント。
            st.age += dt;
        });
}

REGISTER_LOGIC_SYSTEM(BulletTravelSystem, Priority::LogicStage::L02_Update);