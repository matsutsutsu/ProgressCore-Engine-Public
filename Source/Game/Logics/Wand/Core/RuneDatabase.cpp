// ===========================================================================
// RuneDatabase.cpp
// ----------------
// 役割:
//   ルーンのフェーズ別既定パラメータとスタックカーブの実体定義(SSOT)。
//   ★ここの params / カーブ コメントが、後工程ハンドラとの「契約(仕様)」となる。
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
        //   Spawn:  params[0] = 初速倍率（※カーブを持つのでコンパイラが上書きする）
        //   Travel: params[0] = 加速率 / params[1] = 旋回抵抗
        //   OnHit:  params[0] = 貫通時加速倍率
        // -------------------------------------------------------------------
    case EffectType::Accelerate:
        switch (phase)
        {
        case WandPhase::Spawn:  op.params[0] = 1.8f; break;
        case WandPhase::Travel: op.params[0] = 1.5f; op.params[1] = 0.5f; break;
        case WandPhase::OnHit:  op.params[0] = 1.2f; break;
        default: break;
        }
        break;

        // -------------------------------------------------------------------
        // Split : 増殖・拡散
        //   Spawn: params[0]=1スタックあたり加算本数, params[1]=1発あたり扇角[度]
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
        //   Spawn: params[0]=扇角倍率(<1で絞る), params[1]=初速倍率
        //   OnHit: 貫通(フラグのみ・params不要)
        // -------------------------------------------------------------------
    case EffectType::Focus:
        switch (phase)
        {
        case WandPhase::Spawn: op.params[0] = 0.3f; op.params[1] = 1.5f; break;
        default: break;
        }
        break;

        // -------------------------------------------------------------------
        // Slow : 減速・制動
        //   Travel: params[0]=1秒あたりの減速率
        // -------------------------------------------------------------------
    case EffectType::Slow:
        switch (phase)
        {
        case WandPhase::Travel: op.params[0] = 1.2f; break;
        default: break;
        }
        break;

        // -------------------------------------------------------------------
        // Homing : 誘導・追尾
        //   Travel: params[0]=1秒あたりの旋回率
        // -------------------------------------------------------------------
    case EffectType::Homing:
        switch (phase)
        {
        case WandPhase::Travel: op.params[0] = 5.0f; break;
        default: break;
        }
        break;

        // -------------------------------------------------------------------
        // Reflection : 反射（OnHit・フラグのみ・params不要）
        // -------------------------------------------------------------------
    case EffectType::Reflection:
        break;

        // -------------------------------------------------------------------
        // 未実装（placeholder）
        // -------------------------------------------------------------------
    case EffectType::Explosion:
    case EffectType::Chaos:
    default:
        break;
    }

    return op;
}

// ===========================================================================
// スタックカーブ定義（段階導入：カーブを持つ概念だけ true を返す）
// ===========================================================================
bool RuneDatabase::HasStackCurve(EffectType type, WandPhase phase)
{
    // Accelerate の Spawn(初速倍率) のみカーブ化済み。他は従来の params 累乗。
    if (type == EffectType::Accelerate && phase == WandPhase::Spawn) return true;
    return false;
}

RuneStackCurve RuneDatabase::GetStackCurve(EffectType type, WandPhase phase)
{
    if (type == EffectType::Accelerate && phase == WandPhase::Spawn)
    {
        // 初速倍率の逓減カーブ：1個目は強く、重ねるほど伸びが鈍る。
        //   1個目 ×1.80 / 2個目 ×1.50 / 3個目 ×1.30 / 4個目 ×1.20 ...
        //   ※各 value は「その個数目を足したときに speedMult へ掛ける倍率」。
        RuneStackCurve c;
        c.value[0] = 1.80f;
        c.value[1] = 1.50f;
        c.value[2] = 1.30f;
        c.value[3] = 1.20f;
        c.value[4] = 1.12f;
        c.value[5] = 1.08f;
        c.value[6] = 1.05f;
        c.value[7] = 1.03f;
        c.fallback = 1.02f; // 8個を超えたら1個あたり ×1.02（ほぼ頭打ち）
        return c;
    }

    // カーブ未定義の概念は「等倍カーブ」を返す（呼ばれても無害）。
    return RuneStackCurve::Uniform(1.0f);
}