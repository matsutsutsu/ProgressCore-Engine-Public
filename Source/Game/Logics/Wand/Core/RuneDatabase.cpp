// ===========================================================================
// RuneDatabase.cpp
// ----------------
// @file
// @brief ルーンのフェーズ別既定パラメータの実体定義（パラメータの唯一の置き場=SSOT）。
//        ★ここの params コメントが、各 <Name><Phase> ハンドラとの「契約(仕様)」。
// ===========================================================================
#include "RuneDatabase.h"

EffectOp RuneDatabase::GetDefaultOp(EffectType type, WandPhase phase)
{
    EffectOp op;
    op.type = type;
    op.stackCount = 1; // 呼び出し側(コンパイラ)が実際の装着数で上書きする

    switch (type)
    {
        // -------------------------------------------------------------------
        // Accelerate : 速度・運動
        // -------------------------------------------------------------------
    case EffectType::Accelerate:
        switch (phase)
        {
        case WandPhase::Spawn:  op.params[0] = 1.8f; break;                 // 初速倍率
        case WandPhase::Travel: op.params[0] = 1.5f; op.params[1] = 0.5f; break; // 加速率 / 旋回抵抗
        case WandPhase::OnHit:  op.params[0] = 1.2f; break;                 // 貫通時の加速倍率
        default: break;
        }
        break;

        // -------------------------------------------------------------------
        // Split : 増殖・拡散
        //   Spawn:  params[0] = 1スタックあたり加算本数, params[1] = 1発あたり扇角[度]
        // -------------------------------------------------------------------
    case EffectType::Split:
        switch (phase)
        {
        case WandPhase::Spawn: op.params[0] = 2.0f; op.params[1] = 10.0f; break;
        default: break;
        }
        break;

        // -------------------------------------------------------------------
        // Focus : 精密・一点突破
        //   Spawn:  params[0] = 扇角の倍率(<1で絞る/0.0で真っ直ぐ), params[1] = 初速倍率
        //   OnHit:  (Step2) 貫通フラグ
        // -------------------------------------------------------------------
    case EffectType::Focus:
        switch (phase)
        {
        case WandPhase::Spawn: op.params[0] = 0.3f; op.params[1] = 1.5f; break; // 扇を30%に絞る / 初速1.5倍
        default: break;
        }
        break;

        // -------------------------------------------------------------------
        // 以降は実装フェーズで順次定義（現状 placeholder）
        // -------------------------------------------------------------------
    case EffectType::Slow:
    case EffectType::Homing:
    case EffectType::Explosion:
    case EffectType::Reflection:
    case EffectType::Chaos:
    default:
        break;
    }

    return op;
}