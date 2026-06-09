// ===========================================================================
// WandCompiler.cpp
// ----------------
// @file
// @brief WandCompiler の実体。重ねがけ(同概念の複数装着)を stackCount に畳み込む。
// ===========================================================================
#include "WandCompiler.h"
#include "WandBuildComponent.h"
#include "RuneDatabase.h"

uint8_t WandCompiler::CompilePhase(const EffectType* slots, uint8_t slotCount,
                                   WandPhase phase, EffectOp* outOps)
{
    // 1. 概念ごとの装着数を数える（重ねがけの畳み込み）。
    //    EffectType の種類は高々 Count 個と少ないため、固定長カウンタで O(N)。ヒープ確保なし。
    uint16_t counts[static_cast<int>(EffectType::Count)] = {};
    for (uint8_t i = 0; i < slotCount; ++i)
    {
        const EffectType t = slots[i];
        if (t == EffectType::None) continue;
        counts[static_cast<int>(t)]++;
    }

    // 2. enum 昇順で EffectOp を生成（順序を決定的にしておくとデバッグ・再現性に有利）。
    uint8_t opCount = 0;
    for (int t = 1; t < static_cast<int>(EffectType::Count); ++t) // 0 = None はスキップ
    {
        if (counts[t] == 0) continue;
        if (opCount >= MAX_OPS_PER_PHASE) break; // 安全弁（概念数 < 容量なので通常到達しない）

        EffectOp op   = RuneDatabase::GetDefaultOp(static_cast<EffectType>(t), phase);
        op.stackCount = counts[t]; // 装着数を畳んで格納
        outOps[opCount++] = op;
    }
    return opCount;
}

CompiledWandProgram WandCompiler::Compile(const WandBuildComponent& build)
{
    CompiledWandProgram prog;

    // 杖全体のスカラ設定を引き継ぐ
    prog.baseLifetime = build.baseLifetime;
    prog.baseSpeed    = build.baseSpeed;
    prog.cooldown     = build.cooldown;

    constexpr int kSpawn   = static_cast<int>(WandPhase::Spawn);
    constexpr int kTravel  = static_cast<int>(WandPhase::Travel);
    constexpr int kOnHit   = static_cast<int>(WandPhase::OnHit);
    constexpr int kInherit = static_cast<int>(WandPhase::Inheritance);

    prog.spawnCount  = CompilePhase(build.slots[kSpawn],  build.slotCounts[kSpawn],
                                    WandPhase::Spawn,  prog.spawnOps);
    prog.travelCount = CompilePhase(build.slots[kTravel], build.slotCounts[kTravel],
                                    WandPhase::Travel, prog.travelOps);
    prog.onHitCount  = CompilePhase(build.slots[kOnHit],  build.slotCounts[kOnHit],
                                    WandPhase::OnHit,  prog.onHitOps);

    // Inheritance: 現状はスロット内容のデータ捕捉のみ。
    // 「継承スロット→新弾の生成スロットへ焼き直す」正確な意味付けは Step 6 で実装する。
    prog.inheritanceCount = CompilePhase(build.slots[kInherit], build.slotCounts[kInherit],
                                         WandPhase::Inheritance, prog.inheritanceOps);

    return prog;
}