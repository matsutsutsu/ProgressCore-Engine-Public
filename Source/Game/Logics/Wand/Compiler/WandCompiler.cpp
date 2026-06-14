// ===========================================================================
// WandCompiler.cpp
// ----------------
// @file
// @brief WandCompiler の実体。重ねがけ(同概念の複数装着)を stackCount に畳み込む。
// ===========================================================================
#include "Game/Logics/Wand/Compiler/WandCompiler.h"
#include "Game/Logics/Wand/Compiler/WandBuildComponent.h"
#include "Game/Logics/Wand/Core/WandTypes.h"
#include "Game/Logics/Wand/Core/RuneDatabase.h"


// ビルド構成をコンパイルして実行時の最適化された情報に変換する
CompiledWandProgram WandCompiler::Compile(const WandBuildComponent& build)
{
    // 
    CompiledWandProgram prog;

    // 杖全体のスカラ設定を引き継ぐ
    prog.baseDamage = build.baseDamage; // baseSpeed 等と並べてコピー
    prog.baseLifetime = build.baseLifetime;
    prog.baseSpeed = build.baseSpeed;
    prog.cooldown = build.cooldown;

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

    // 2. enum 昇順で EffectOp を生成（順序を決定的にしておくとデバッグ・再現性に有利）。
    uint8_t opCount = 0;
    for (int t = 1; t < static_cast<int>(EffectType::Count); ++t) // 0 = None はスキップ
    {
        // 上の処理でアタッチメント装着数が１つでも入っていたら内部処理に進む
        if (counts[t] == 0) continue;
        if (opCount >= MAX_OPS_PER_PHASE) break; // 安全弁（概念数 < 容量なので通常到達しない）

        // アタッチメントの初期パラメータをセット
        EffectOp op   = RuneDatabase::GetDefaultOp(static_cast<EffectType>(t), phase);
        op.stackCount = counts[t]; // 装着数を畳んで格納
        // 最後はそのフェーズのEffectOpの配列変数に今のアタッチメント情報を格納する
        outOps[opCount++] = op;
    }

    //【出力: outOps】（圧縮された戦闘用データ）
    //outOps[0] = { type: Accelerate, stackCount : 2, params : [...] }
    //outOps[1] = { type: Split,      stackCount : 1, params : [...] }
    //
    //戻り値(opCount) = 2  （有効な命令は 2 個だけですよ、と伝える）

    return opCount;
}
