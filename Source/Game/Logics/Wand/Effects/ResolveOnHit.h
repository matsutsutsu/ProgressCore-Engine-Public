#pragma once
// ===========================================================================
// Effects/ResolveOnHit.h
// ----------------------
// 役割:
//   OnHitフェーズの集約器。prog.onHitOps を走査し、各 <Name>OnHit ハンドラを呼んで
//   OnHitResolution を組み立てる。壁命中System・敵命中System の双方が共有する。
//
// 設計方針:
//   - Spawn の ResolveSpawn / Travel の ResolveTravel と同じ「switch でハンドラ分岐」。
//     ヘッダに inline で置くことで、壁・敵の両.cppから重複なく使える。
//   - ここは解釈(どう振る舞うかの確定)だけ。実際の弾の生死・反射計算・ダメージは
//     呼び出し側のSystemが結果を見て行う(関心の分離)。
// ===========================================================================
#include "Game/Logics/Wand/Core/WandTypes.h"
#include "Game/Logics/Wand/Compiler/CompiledWandProgram.h"
#include "Game/Logics/Wand/Effects/OnHitResolution.h"
#include "Game/Logics/Wand/Effects/Effect_Focus.h"      // FocusOnHit
#include "Game/Logics/Wand/Effects/Effect_Reflection.h" // ReflectionOnHit

/// @brief onHitOps を解釈して OnHitResolution を返す（壁・敵命中の共通処理）。
inline OnHitResolution ResolveOnHit(const CompiledWandProgram& prog)
{
    OnHitResolution r;
    for (uint8_t i = 0; i < prog.onHitCount; ++i)
    {
        const EffectOp& op = prog.onHitOps[i];
        switch (op.type)
        {
        case EffectType::Focus:      FocusOnHit(op, r);      break;
        case EffectType::Reflection: ReflectionOnHit(op, r); break;
            // case EffectType::Explosion: ExplosionOnHit(op, r); ← 次ステップ(範囲2)で追加
        default: break;
        }
    }
    return r;
}