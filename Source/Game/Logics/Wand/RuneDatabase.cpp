// ===========================================================================
// RuneDatabase.cpp
// ----------------
// @file
// @brief ルーンのフェーズ別既定パラメータの実体定義。
//        ★ここの params コメントが、後工程のハンドラとの「契約(仕様)」となる。
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
        // Accelerate : 速度・運動（Step 3 で全フェーズ実装する先行定義）
        // -------------------------------------------------------------------
    case EffectType::Accelerate:
        switch (phase)
        {
        case WandPhase::Spawn:
            op.params[0] = 1.8f; // params[0] = 初速倍率
            break;
        case WandPhase::Travel:
            op.params[0] = 1.5f; // params[0] = 1秒あたりの加速率 (speed *= 1 + rate*dt)
            op.params[1] = 0.5f; // params[1] = 旋回抵抗（向き変更のしにくさ。0で無効）
            break;
        case WandPhase::OnHit:
            op.params[0] = 1.2f; // params[0] = 貫通時の加速倍率
            break;
        default:
            break; // Inheritance は別途 Spawn として焼き直すため個別定義不要
        }
        break;

        // -------------------------------------------------------------------
        // 以降は実装フェーズで順次定義（現状は type のみ・params=0 のプレースホルダ）
        // -------------------------------------------------------------------
    case EffectType::Slow:
    case EffectType::Focus:
    case EffectType::Homing:
    case EffectType::Explosion:
    case EffectType::Split:
    case EffectType::Reflection:
    case EffectType::Chaos:
    default:
        break;
    }

    return op;
}