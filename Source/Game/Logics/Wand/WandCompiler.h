#pragma once
// ===========================================================================
// WandCompiler.h
// --------------
// @file
// @brief オーサリング(WandBuildComponent)を実行時不変な CompiledWandProgram へ
//        変換する純粋ロジック。状態を持たない静的関数のみ。
//
// 変換の核:
//   各フェーズのスロット列を走査し、同一概念(EffectType)の重複を stackCount に
//   畳み込んで EffectOp 列を生成する。params は RuneDatabase から取得する。
//
// @note 副作用なし(World/Registry に触れない)。Registry への登録は WandCompileSystem
//       の責務であり、本クラスは「変換」だけに専念する（テスト容易性のため）。
// ===========================================================================

#include "WandTypes.h"
#include "CompiledWandProgram.h"

struct WandBuildComponent; // 前方宣言（実体は WandBuildComponent.h、.cpp 側で include）

/**
 * @brief WandBuildComponent → CompiledWandProgram への純粋変換器。
 */
struct WandCompiler
{
    /**
     * @brief ビルド構成を​コンパイルして​実行時の​最適化された​情報に​変換する​
     * @param build オーサリング構成
     * @return コンパイル済みプログラム（POD の値返し。コピーは安価）
     */
    static CompiledWandProgram Compile(const WandBuildComponent& build);

private:
    /**
     * @brief 1フェーズ分のスロット列を EffectOp 列へ畳み込む。
     * @param slots     スロット配列の先頭
     * @param slotCount 有効スロット数
     * @param phase     対象フェーズ（params 取得に使用）
     * @param outOps    出力先（MAX_OPS_PER_PHASE 個の容量を前提）
     * @return 生成した EffectOp 数
     */
    static uint8_t CompilePhase(const EffectType* slots, uint8_t slotCount,
        WandPhase phase, EffectOp* outOps);
};