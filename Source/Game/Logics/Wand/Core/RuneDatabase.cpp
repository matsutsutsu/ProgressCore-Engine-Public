// ===========================================================================
// RuneDatabase.cpp
// ----------------
// 役割:
//   ルーンのフェーズ別既定パラメータ・スタックカーブ・畳み込み方の実体定義(SSOT)。
//   ★ここの値とコメントが、後工程ハンドラ・コンパイラとの「契約(仕様)」になる。
//
// 新しいカーブを追加するときのチェックリスト:
//   [1] HasStackCurve  … true を返す条件に追記
//   [2] GetStackCurve  … カーブの数値テーブルを追記
//   [3] GetCurveFoldType … 倍率系(Multiplicative) か 加算系(Additive) かを追記
//   ※ ハンドラ・WandCompiler は変更不要
// ===========================================================================
#include "RuneDatabase.h"

// ---------------------------------------------------------------------------
// GetDefaultOp：フェーズ別の既定パラメータ
// ---------------------------------------------------------------------------
EffectOp RuneDatabase::GetDefaultOp(EffectType type, WandPhase phase)
{
    EffectOp op;
    op.type = type;
    op.stackCount = 1;

    switch (type)
    {
    case EffectType::Accelerate:
        switch (phase)
        {
        case WandPhase::Spawn:  op.params[0] = 1.8f;                      break;
        case WandPhase::Travel: op.params[0] = 1.5f; op.params[1] = 0.5f; break;
        case WandPhase::OnHit:  op.params[0] = 1.2f;                      break;
        default: break;
        }
        break;

    case EffectType::Split:
        switch (phase)
        {
        case WandPhase::Spawn: op.params[0] = 2.0f; op.params[1] = 10.0f; break;
        default: break;
        }
        break;

    case EffectType::Focus:
        switch (phase)
        {
        case WandPhase::Spawn: op.params[0] = 0.3f; op.params[1] = 1.5f; break;
        default: break;
        }
        break;

    case EffectType::Slow:
        switch (phase)
        {
        case WandPhase::Travel: op.params[0] = 1.2f; break;
        default: break;
        }
        break;

    case EffectType::Homing:
        switch (phase)
        {
        case WandPhase::Travel: op.params[0] = 5.0f; break;
        default: break;
        }
        break;

    case EffectType::Reflection:
    case EffectType::Explosion:
    case EffectType::Chaos:
    default:
        break;
    }

    return op;
}

// ---------------------------------------------------------------------------
// HasStackCurve：カーブを持つ (type, phase) の一覧
// ---------------------------------------------------------------------------
bool RuneDatabase::HasStackCurve(EffectType type, WandPhase phase)
{
    // [追加手順] カーブを付けたい概念を ここに追記するだけ。
    if (type == EffectType::Accelerate && phase == WandPhase::Spawn) return true;
    // if (type == EffectType::Split && phase == WandPhase::Spawn)   return true; // 追加例
    return false;
}

// ---------------------------------------------------------------------------
// GetStackCurve：スタック数ごとの効果値テーブル
// ---------------------------------------------------------------------------
RuneStackCurve RuneDatabase::GetStackCurve(EffectType type, WandPhase phase)
{
    // -------------------------------------------------------------------
    // Accelerate × Spawn : 初速倍率の逓減カーブ（倍率系・乗算で畳む）
    //   value[i] = (i+1)個目のスタックを足したときに speedMult へ掛ける倍率
    //   1個目 ×1.80 / 2個目 ×1.50 / 3個目 ×1.30 ... 8個以降 ×1.02
    // -------------------------------------------------------------------
    if (type == EffectType::Accelerate && phase == WandPhase::Spawn)
    {
        RuneStackCurve c;
        c.value[0] = 1.50f;
        c.value[1] = 1.40f;
        c.value[2] = 1.30f;
        c.value[3] = 1.12f;
        c.value[4] = 1.08f;
        c.value[5] = 1.08f;
        c.value[6] = 1.05f;
        c.value[7] = 1.03f;
        c.fallback = 1.02f;
        return c;
    }

    // -------------------------------------------------------------------
    // Split × Spawn（追加例：コメントを外せば有効）
    //   value[i] = (i+1)個目のスタックで増える弾数
    //   1・2個目 +2本 / 3個目以降 +1本（逓減）
    // -------------------------------------------------------------------
    //if (type == EffectType::Split && phase == WandPhase::Spawn)
    //{
    //    RuneStackCurve c;
    //    c.value[0] = 2.0f;
    //    c.value[1] = 2.0f;
    //    c.value[2] = 1.0f;
    //    c.value[3] = 1.0f;
    //    c.value[4] = 1.0f;
    //    c.value[5] = 1.0f;
    //    c.value[6] = 1.0f;
    //    c.value[7] = 1.0f;
    //    c.fallback  = 1.0f;
    //    return c;
    //}

    return RuneStackCurve::Uniform(1.0f); // 未定義は等倍（無害）
}

// ---------------------------------------------------------------------------
// GetCurveFoldType：カーブをどう畳み込むか（概念の性質で決まる）
// ---------------------------------------------------------------------------
CurveFoldType RuneDatabase::GetCurveFoldType(EffectType type, WandPhase /*phase*/)
{
    switch (type)
    {
        // 加算系：本数・角度など「足し算で効く」概念
    case EffectType::Split: return CurveFoldType::Additive;

        // 倍率系：速度倍率など「掛け算で効く」概念（デフォルト）
    default:                return CurveFoldType::Multiplicative;
    }
}