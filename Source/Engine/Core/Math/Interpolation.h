#pragma once
// ===========================================================================
// 概要: 補間で知らないと間違える式（Damp・SmoothStep）を、理由付きで1か所に置く
//
// 備考: 線形補間は std::lerp や Vector3::Lerp、クランプは std::clamp を使い、ここには置かない
//       調整したいイージングはカーブ（CurveData.h）に寄せる。固定の関数を並べると役割が競合する
//       短いからではなく、間違えやすいから置く。基準を外すと、全員が読む巨大なヘッダになる
// ===========================================================================
#include <cmath>

namespace CCL::Math
{

    // フレームレートによらない追従の係数。std::lerp(今の値, 目標, Damp(speed, dt)) の形で使う
    // lerp(今の値, 目標, speed * dt) だと係数がフレーム時間に比例し、60fps と 144fps で追従の速さが変わる
    inline float Damp(float speed, float dt)
    {
        return 1.0f - std::exp(-speed * dt);
    }

    // 0〜1 を S 字にする（両端で速度が 0）。線形だと始まりと終わりが唐突に見える演出に使う
    inline float SmoothStep(float t)
    {
        if (t <= 0.0f) return 0.0f;
        if (t >= 1.0f) return 1.0f;
        return t * t * (3.0f - 2.0f * t);
    }

    // 0〜1 を S 字にする（両端で加速度まで 0）
    // 区間の境目で加速度が折れないので、ノイズの格子の補間のように、境目が周期的なカクつきに見える用途に使う
    inline float SmootherStep(float t)
    {
        if (t <= 0.0f) return 0.0f;
        if (t >= 1.0f) return 1.0f;
        return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
    }

}
