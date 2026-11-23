#pragma once
// ブレンドスペースの重みの計算と、入力の追従
// 実行時（AnimControllerSystem）とエディタの図（AnimGraphWindow）の両方が使う。点の位置は配列で受け取る
#include "Engine/Animation/Core/AnimStateMachine.h"
#include <algorithm>
#include <cmath>

namespace CCL::Anim
{
    // 1 つのブレンドスペースに置ける点の上限（実行時の重みを固定長の配列で持つため）
    inline constexpr int kMaxBlendSamples = 8;



    // 今の値を目標へ寄せる。time 秒で差の約 63% が埋まる（指数的に近づく）。time が 0 以下ならすぐ目標にする
    //   毎ステップ一定の割合で寄せるのと違い、dt が変わっても同じ時間で同じだけ寄る
    inline float SmoothToward(float current, float target, float time, float dt)
    {
        if (time <= 0.0f)
        {
            return target;
        }
        const float rate = 1.0f - std::exp(-dt / time);
        return current + (target - current) * rate;
    }



    // 角度を [0, 2π) に収める
    inline float WrapAnglePositive(float angle)
    {
        constexpr float kTwoPi = 6.28318530718f;
        angle                  = std::fmod(angle, kTwoPi);
        if (angle < 0.0f)
        {
            angle += kTwoPi;
        }
        return angle;
    }



    // 1D: 入力をはさむ両隣の 2 点を、距離の比で混ぜる。範囲の外は端の点だけを使う
    inline void ComputeWeights1D(const float *xs, int count, float input, float *outWeights)
    {
        // 入力以下で一番近い点（lower）と、入力以上で一番近い点（upper）
        int lower = -1;
        int upper = -1;
        for (int i = 0; i < count; ++i)
        {
            if (xs[i] <= input && (lower < 0 || xs[i] > xs[lower]))
            {
                lower = i;
            }
            if (xs[i] >= input && (upper < 0 || xs[i] < xs[upper]))
            {
                upper = i;
            }
        }

        if (lower < 0)
        {
            outWeights[upper] = 1.0f;
            return;
        }
        if (upper < 0)
        {
            outWeights[lower] = 1.0f;
            return;
        }

        const float span = xs[upper] - xs[lower];
        if (lower == upper || span < 1e-6f)
        {
            outWeights[lower] = 1.0f;
            return;
        }

        const float t      = (input - xs[lower]) / span;
        outWeights[lower] = 1.0f - t;
        outWeights[upper] = t;
    }



    // 2D の方向型: 入力の向きをはさむ 2 方向を、角度の比で混ぜる
    //   原点に点（中心）があれば、入力の長さで中心とも混ぜる（長さ 0 で中心だけ、外周で方向だけ）
    inline void ComputeWeightsDirectional2D(const float *xs, const float *ys, int count, float inputX, float inputY,
                                            float *outWeights)
    {
        // 処理フロー:
        // 1. 中心の点を探す
        // 2. 入力の向きをはさむ 2 点を探す
        // 3. 2 点を角度の比で混ぜ、中心があれば長さで中心とも混ぜる

        // ---------------------------------------------------------
        // 1. 中心
        // ---------------------------------------------------------

        // 中心は向きを持たないので、2. の候補から外す
        constexpr float kCenterEpsilon = 1e-4f;
        int             center         = -1;
        for (int i = 0; i < count; ++i)
        {
            if (xs[i] * xs[i] + ys[i] * ys[i] <= kCenterEpsilon * kCenterEpsilon)
            {
                center = i;
                break;
            }
        }

        // 入力がほぼ 0 なら向きが決まらない。中心があれば中心、なければ時間の基準の点だけを使う
        const float inputLength = std::sqrt(inputX * inputX + inputY * inputY);
        if (inputLength < kCenterEpsilon)
        {
            outWeights[center >= 0 ? center : 0] = 1.0f;
            return;
        }

        // ---------------------------------------------------------
        // 2. 入力の向きをはさむ 2 点
        // ---------------------------------------------------------

        // 左回り（反時計回り）に一番近い点 next と、右回りに一番近い点 prev
        const float inputAngle = std::atan2(inputY, inputX);
        int         prev       = -1;
        int         next       = -1;
        float       prevGap    = 0.0f; // prev から入力までの角度
        float       nextGap    = 0.0f; // 入力から next までの角度
        for (int i = 0; i < count; ++i)
        {
            if (i == center)
            {
                continue;
            }

            const float sampleAngle = std::atan2(ys[i], xs[i]);
            const float ccw         = WrapAnglePositive(sampleAngle - inputAngle);
            const float cw          = WrapAnglePositive(inputAngle - sampleAngle);
            if (next < 0 || ccw < nextGap)
            {
                next    = i;
                nextGap = ccw;
            }
            if (prev < 0 || cw < prevGap)
            {
                prev    = i;
                prevGap = cw;
            }
        }

        // 方向の点がない（中心だけ）
        if (prev < 0)
        {
            outWeights[center] = 1.0f;
            return;
        }

        // ---------------------------------------------------------
        // 3. 混ぜる
        // ---------------------------------------------------------

        // 入力が prev にちょうど重なれば t = 0（prev だけ）、next に重なれば t = 1（next だけ）
        const float span = prevGap + nextGap;
        const float t    = (span > 1e-6f) ? (prevGap / span) : 0.0f;

        float prevWeight = 1.0f - t;
        float nextWeight = t;

        if (center >= 0)
        {
            // 外周の半径（2 点の原点からの距離を、同じ比で混ぜたもの）に対する入力の長さ
            const float prevRadius = std::sqrt(xs[prev] * xs[prev] + ys[prev] * ys[prev]);
            const float nextRadius = std::sqrt(xs[next] * xs[next] + ys[next] * ys[next]);
            const float ringRadius = prevRadius + (nextRadius - prevRadius) * t;
            const float outer      = std::clamp(inputLength / ringRadius, 0.0f, 1.0f);

            outWeights[center] = 1.0f - outer;
            prevWeight *= outer;
            nextWeight *= outer;
        }

        // prev と next が同じ点（入力がちょうど点の向き、または方向の点が 1 つだけ）のときに備えて足し込む
        outWeights[prev] += prevWeight;
        outWeights[next] += nextWeight;
    }



    // 入力の位置から、count 個の点の重み（合計 1）を求める。1D では ys と inputY を使わない
    inline void ComputeBlendWeights(AnimBlendSpaceType type, const float *xs, const float *ys, int count,
                                    float inputX, float inputY, float *outWeights)
    {
        for (int i = 0; i < count; ++i)
        {
            outWeights[i] = 0.0f;
        }
        if (count <= 0)
        {
            return;
        }

        if (type == AnimBlendSpaceType::Blend1D)
        {
            ComputeWeights1D(xs, count, inputX, outWeights);
        }
        else
        {
            ComputeWeightsDirectional2D(xs, ys, count, inputX, inputY, outWeights);
        }
    }
}
