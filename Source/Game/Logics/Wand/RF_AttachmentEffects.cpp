// ===========================================================================
// @file   RF_AttachmentEffects.cpp
// @brief  RF_AttachmentEffects.h で宣言した、アタッチメント効果のフェーズ別
//         適用と弾生成の実体。プロト段階では Accelerate を4フェーズ全実装する。
//
// @note   実装の Why:
//         効果は「BulletState を書き換えるだけ」に徹し、座標積分や衝突は呼び出し側
//         システムが担う。これにより同じ効果関数を発射経路・継承経路の双方から
//         再利用でき、単体テストも純粋関数として書ける。
// ===========================================================================

#include "RF_AttachmentEffects.h"

#include "Engine/ECS/Core/CCL_World.h"             // @note World 完全定義。実パスに調整。
#include "Engine/ECS/Common/CCL_Archetype.h"          // @note ArchetypeHelper。実パスに調整。
#include "Engine/Transform/Components/TransformComponent.h" // @note position を持つ前提。実パスに調整。
#include "Engine/Timer/TimeState.h"  // @note localDt を持つ前提。実パスに調整。
#include "Engine/Platform/Core/Logger.h"                 // @note std::cout 禁止。CCL_LOG を使用。

using namespace CCL::ECS;
using namespace CCL::ECS::Core;
using namespace RuneForge;


// ---------------------------------------------------------------------------
// Spawn フェーズ
// ---------------------------------------------------------------------------
void RuneForge::ApplySpawnPhase(const CompiledWandProgram& prog,
                                const Vector3&             aimDir,
                                std::vector<BulletInit>&   out)
{
    // 基準弾を1発置く。以降の op はこのリストを加工していく。
    BulletInit base;
    base.dirX = aimDir.x; base.dirY = aimDir.y; base.dirZ = aimDir.z;
    base.speedMul = 1.0f;
    out.push_back(base);

    for (const EffectOp& op : prog.phases[PhaseIndex(Phase::Spawn)])
    {
        switch (op.type)
        {
        case AttachmentType::Accelerate:
            // 初速倍率: 既存の全弾に乗算（重ねがけ＝複数回乗算で自然に強化される）
            for (BulletInit& b : out) b.speedMul *= op.p0;
            break;

        // @todo Split: out に左右拡散の要素を追加 / Focus: 拡散を0に / Reflection: 逆方向弾を追加 …
        default:
            break;
        }
    }
}


// ---------------------------------------------------------------------------
// Travel フェーズ（毎フレーム）
// ---------------------------------------------------------------------------
void RuneForge::ApplyTravelPhase(const CompiledWandProgram& prog,
                                 BulletState&               st,
                                 float                      localDt)
{
    for (const EffectOp& op : prog.phases[PhaseIndex(Phase::Travel)])
    {
        switch (op.type)
        {
        case AttachmentType::Accelerate:
            // 毎秒 p0 だけ速度倍率を加算（連続加速）。localDt 基準なので
            // ヒットストップ中は加速も止まる（時間データ化の恩恵）。
            st.speedMul += op.p0 * localDt;
            break;

        // @todo Slow: speedMul を減衰させ 0 で停止 / Homing は専用システムが処理 …
        default:
            break;
        }
    }
}


// ---------------------------------------------------------------------------
// OnHit フェーズ（衝突の瞬間）
// ---------------------------------------------------------------------------
void RuneForge::ApplyHitPhase(const CompiledWandProgram& prog,
                              BulletState&               st,
                              const BulletHitEvent&      /*hit*/)
{
    for (const EffectOp& op : prog.phases[PhaseIndex(Phase::OnHit)])
    {
        switch (op.type)
        {
        case AttachmentType::Accelerate:
            // 仕様: 「貫通時にさらに加速」。貫通が残っている弾にのみ作用する。
            // （貫通を与える Focus 等が未装着なら no-op になるのが正しい挙動）
            if (st.pierceLeft > 0) st.speedMul *= op.p0;
            break;

        // @todo Explosion: 着弾点に爆発生成 / Reflection: 反射 / Split: 3方向分裂 …
        default:
            break;
        }
    }
}


// ---------------------------------------------------------------------------
// 弾生成（発射・継承の共通経路）
// ---------------------------------------------------------------------------
void RuneForge::SpawnBulletsFromProgram(World&               world,
                                        WandProgramRegistry& reg,
                                        ProgramID            programId,
                                        const Vector3&       origin,
                                        const Vector3&       aimDir,
                                        std::uint8_t         faction,
                                        float                baseSpeed,
                                        float                baseLife)
{
    if (programId == InvalidProgramID) return;

    const CompiledWandProgram& prog = reg.Get(programId);

    // Spawn フェーズで「何発を、どの方向・初速で」出すかを決める。
    // 発射はクールダウンで律速され低頻度のため、ここでの小さな vector 確保は許容。
    // （毎フレーム数千回の最内ループではないので #85 のヒープ禁止には抵触しない）
    std::vector<BulletInit> inits;
    ApplySpawnPhase(prog, aimDir, inits);

    // 弾の基本 Archetype は常に同一（ビルド差は programId が吸収）。
    // TimeStateComponent を含めるのは規約#9: 弾も個別時間(localDt)で動くため。
    const Archetype bulletArch =
        ArchetypeHelper::Generate<TransformComponent, TimeStateComponent,
                                  BulletState, BulletProgramRef>();

    for (const BulletInit& init : inits)
    {
        Vector3 dir(init.dirX, init.dirY, init.dirZ);
        if (dir.LengthSquared() > 1e-8f) dir.Normalize();
        else                              dir = Vector3(0, 0, 1);

        BulletState st;
        st.velocity      = dir * baseSpeed; // 方向×基準速度。加減速は speedMul 側。
        st.speedMul      = init.speedMul;
        st.lifeRemaining = baseLife;
        st.pierceLeft    = 0; // @todo Focus 等の貫通付与アタッチメントから設定する
        st.bounceLeft    = 0; // @todo Reflection から設定する

        // Spawn → Set（基本構成は上書き）→ Patch（座標だけ差し替え）。
        // 全て PendingOps 経由で遅延適用されるため、システムのループ内でも安全。
        const EntityID id =
            world.Spawn(bulletArch)
                 .Set(st)
                 .Set(BulletProgramRef{ programId, faction })
                 .Patch<TransformComponent>([&](auto& t) { t.position = origin; });

        // 任意挙動コンポーネントの条件付き付与（Archetype を少数に保つ要）
        if (prog.optionalMask & OPT_HOMING)
            world.AddComponent<HomingState>(id, HomingState{});
        if (prog.optionalMask & OPT_PERIODIC_BLAST)
            world.AddComponent<PeriodicBlastTimer>(id, PeriodicBlastTimer{});
    }
}
