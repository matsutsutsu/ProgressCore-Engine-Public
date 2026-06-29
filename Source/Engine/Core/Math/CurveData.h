#pragma once
#include <vector>
#include <string>
#include <algorithm>

enum class CurveInterpolation {
    Constant, // カクッと切り替わる
    Linear,   // 直線補間
    Cubic     // なめらかなスプライン/ベジェ補間
};

struct CurveKeyframe {
    float time;
    float value;
    float inTangent  = 0.0f; // 接線（Cubic用）
    float outTangent = 0.0f;
    CurveInterpolation interp = CurveInterpolation::Linear;
    

};

struct CurveData {
    std::string name;
    std::vector<CurveKeyframe> keys;

    // 時間tにおけるY値を評価する
    float Evaluate(float t) const {
        if (keys.empty()) return 0.0f;
        if (keys.size() == 1 || t <= keys.front().time) return keys.front().value;
        if (t >= keys.back().time) return keys.back().value;

        // 二分探索で挟まれているキーを探す (O(logN))
        auto it = std::lower_bound(keys.begin(), keys.end(), t, 
            [](const CurveKeyframe& a, float val) { return a.time < val; });
        
        auto k1 = it;
        auto k0 = it - 1;

        float dt = k1->time - k0->time;
        float factor = (dt > 0.0f) ? (t - k0->time) / dt : 0.0f;

        switch (k0->interp) {
            case CurveInterpolation::Constant: return k0->value;
            case CurveInterpolation::Linear:   return k0->value + (k1->value - k0->value) * factor;
            case CurveInterpolation::Cubic: {
                // エルミートスプライン補間
                float f2 = factor * factor;
                float f3 = f2 * factor;
                float w0 = 2.0f * f3 - 3.0f * f2 + 1.0f;
                float w1 = -2.0f * f3 + 3.0f * f2;
                float w2 = f3 - 2.0f * f2 + factor;
                float w3 = f3 - f2;
                return (w0 * k0->value) + (w1 * k1->value) + (w2 * k0->outTangent * dt) + (w3 * k1->inTangent * dt);
            }
        }
        return k0->value;
    }

};