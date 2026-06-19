// ===========================================================================
// WandCompiler.cpp
// ----------------
// 役割:
//   オーサリング(WandBuildComponent)を実行時不変な CompiledWandProgram へ変換する。
//
// 実装の意図(Why):
//   - 「何をカーブ化するか」「どう畳み込むか」はRuneDatabase の責務。
//     WandCompiler はその答えを受け取って汎用的に処理するだけ（知識を持たない）。
//   - カーブを持つ概念は params に畳み込み済みの値を焼き、stackCount=1 にする。
//     実行時ハンドラはカーブを知らずに params を1回参照するだけで済む。
// ===========================================================================
#include "Game/Logics/Wand/Compiler/WandCompiler.h"
#include "Game/Logics/Wand/Compiler/WandBuildComponent.h"
#include "Game/Logics/Wand/Core/WandTypes.h"
#include "Game/Logics/Wand/Core/RuneDatabase.h"

#include <cstring> // strcpy_s

namespace
{
    // -----------------------------------------------------------------------
     // カーブ畳み込み関数群
     // 「どの概念がどちらを使うか」は RuneDatabase::GetCurveFoldType で決める。
     // -----------------------------------------------------------------------

     /// @brief 倍率系：At(0)*At(1)*...*At(stack-1)。
     ///        Accelerate の初速倍率など「掛け算で効く」概念に使う。
    float FoldMultiplicative(const RuneStackCurve& curve, uint16_t stackCount)
    {
        float acc = 1.0f;
        for (uint16_t s = 0; s < stackCount; ++s)
            acc *= curve.At(static_cast<int>(s));
        return acc;
    }

    /// @brief 加算系：At(0)+At(1)+...+At(stack-1)。
    ///        Split の本数加算など「足し算で効く」概念に使う。
    float FoldAdditive(const RuneStackCurve& curve, uint16_t stackCount)
    {
        float acc = 0.0f;
        for (uint16_t s = 0; s < stackCount; ++s)
            acc += curve.At(static_cast<int>(s));
        return acc;
    }
}


// ビルド構成をコンパイルして実行時の最適化された情報に変換する
CompiledWandProgram WandCompiler::Compile(const WandBuildComponent& build)
{
    CompiledWandProgram prog;

    // --- 杖全体のスカラ設定をそのまま写す（加工しない） ---
    prog.baseLifetime = build.baseLifetime;
    prog.baseSpeed = build.baseSpeed;
    prog.cooldown = build.cooldown;

    // 戦闘の基礎値
    prog.baseDamage = build.baseDamage;
    prog.baseKnockback = build.baseKnockback;
    prog.baseCritRate = build.baseCritRate;
    prog.baseCritMult = build.baseCritMult;

    // 見た目VFXパス（固定長char配列のコピー）
    strcpy_s(prog.bulletVfxPath, sizeof(prog.bulletVfxPath), build.bulletVfxPath);


    // フェーズを整数に変換する処理
    constexpr int kSpawn   = static_cast<int>(WandPhase::Spawn);
    constexpr int kTravel  = static_cast<int>(WandPhase::Travel);
    constexpr int kOnHit   = static_cast<int>(WandPhase::OnHit);
    constexpr int kInherit = static_cast<int>(WandPhase::Inheritance);

    // １フェーズ分のスロット列をEffectOpsのフェーズ配列に変換して
    // spawnOpsの配列変数に格納する

    // slots[フェーズ][アタッチメントスロット数]
    prog.spawnCount = CompilePhase(
        build.slots[kSpawn],       // ① 「Spawnフェーズの横1列（スロット8個分）のデータを渡すから...」
        build.slotCounts[kSpawn],  // ② 「そのうち、有効なスロットの数（例えば3つ）だけを見て...」
        WandPhase::Spawn,          // ③ 「Spawnフェーズ用の初期パラメータを使って...」
        prog.spawnOps              // ④ 「結果をこの戦闘用配列（spawnOps）に書き込んでね！」
    );

    prog.travelCount = CompilePhase(
        build.slots[kTravel],
        build.slotCounts[kTravel],
        WandPhase::Travel, prog.travelOps);

    prog.onHitCount = CompilePhase(
        build.slots[kOnHit],
        build.slotCounts[kOnHit],
        WandPhase::OnHit,
        prog.onHitOps);


    // Inheritance: 継承スロットは「次弾の生成」として焼く。phaseは Spawn を渡す。
    prog.inheritanceCount = CompilePhase(
        build.slots[kInherit],
        build.slotCounts[kInherit],
        WandPhase::Spawn,
        prog.inheritanceOps);

    return prog;
}


uint8_t WandCompiler::CompilePhase(const EffectType* slots, uint8_t slotCount,
                                   WandPhase phase, EffectOp* outOps)
{
    // 1. 概念ごとの装着数を数える（重ねがけの畳み込み）。
    //    EffectType の種類は高々 Count 個と少ないため、固定長カウンタで O(N)。ヒープ確保なし。

    // 内部処理イメージ：
    //【counts 配列（集計結果）】※インデックスが EffectType の番号に対応
    //    counts[1](Accelerate) = 2
    //    counts[2](Slow) = 0
    //    counts[3](Focus) = 0
    //    counts[4](Homing) = 0
    //    counts[5](Explosion) = 0
    //    counts[6](Split) = 1

    // 固定長配列にすることでスタックメモリに配列を作成する高速化
    uint16_t counts[static_cast<int>(EffectType::Count)] = {};
    for (uint8_t i = 0; i < slotCount; ++i)
    {
        const EffectType t = slots[i];
        if (t == EffectType::None) continue;
        counts[static_cast<int>(t)]++;
    }

    // 2. enum 昇順で EffectOp を生成。
    uint8_t opCount = 0;
    for (int t = 1; t < static_cast<int>(EffectType::Count); ++t) // 0 = None はスキップ
    {
        if (counts[t] == 0) continue;
        if (opCount >= MAX_OPS_PER_PHASE) break;

        const EffectType type = static_cast<EffectType>(t);
        const uint16_t   stack = counts[t];

        EffectOp op = RuneDatabase::GetDefaultOp(type, phase);

        // --- スタックカーブを持つ概念は、コンパイル時にカーブを畳み込む ---
        if (RuneDatabase::HasStackCurve(type, phase))
        {
            const RuneStackCurve curve = RuneDatabase::GetStackCurve(type, phase);
            const CurveFoldType  foldType = RuneDatabase::GetCurveFoldType(type, phase);

            switch (foldType)
            {
            case CurveFoldType::Multiplicative:
                op.params[0] = FoldMultiplicative(curve, stack);
                op.stackCount = 1;
                break;
            case CurveFoldType::Additive:
                op.params[0] = FoldAdditive(curve, stack);
                op.stackCount = 1;
                break;
            }
        }
        else
        {
            op.stackCount = stack;
        }

        outOps[opCount++] = op;
    }

    return opCount;
}
