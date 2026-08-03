#pragma once
#include <vector>
#include <string>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <json.hpp>

// ===========================================================================
// カーブのコアデータモデル（エディタ非依存 / ランタイムからも直接使える）
// ===========================================================================

// キー区間の補間方式（あるキーの「右側の区間」の形状を決める）
enum class CurveInterp : uint8_t
{
    Constant, // ステップ（次のキーまで値を保持）
    Linear,   // 直線
    Cubic     // エルミート曲線（接線で滑らかに補間）
};

// 接線モード（Cubic のときの in/out 接線の決め方）
enum class TangentMode : uint8_t
{
    Auto,    // 前後キーから自動算出（滑らか）
    Flat,    // 水平（傾き0）
    Aligned, // 手動・in/out が一直線（角のない滑らかさ）
    Broken   // 手動・in/out 独立（角を作れる）
};

// カーブ範囲外（両端の外側）を評価するときの振る舞い
enum class ExtrapMode : uint8_t
{
    Constant,   // 端の値を保持
    Linear,     // 端の傾きで直線的に延長
    Cycle,      // ループ
    PingPong,   // 折り返し
    CycleOffset // ループ＋値の差分を累積（右肩上がりループ等）
};

struct CurveKeyframe
{
    float       time       = 0.0f;
    float       value      = 0.0f;
    float       inTangent  = 0.0f; // 傾き dy/dx（左側）
    float       outTangent = 0.0f; // 傾き dy/dx（右側）
    CurveInterp interp     = CurveInterp::Cubic;
    TangentMode tangentIn  = TangentMode::Auto;
    TangentMode tangentOut = TangentMode::Auto;

    uint32_t id = 0; // ランタイム専用の安定ID（ソートしても選択を追える）。シリアライズしない
};

struct CurveData
{
    std::string                name         = "Curve";
    uint32_t                   color        = 0xFF80FF00u; // IM_COL32(0,255,128,255) 相当（0xAABBGGRR）
    bool                       visible      = true;
    ExtrapMode                 preInfinity  = ExtrapMode::Constant;
    ExtrapMode                 postInfinity = ExtrapMode::Constant;
    std::vector<CurveKeyframe> keys;

    uint32_t m_nextId = 1; // ID採番カウンタ

    // -----------------------------------------------------------------------
    // 基本操作
    // -----------------------------------------------------------------------
    bool empty() const
    {
        return keys.empty();
    }
    uint32_t GenId()
    {
        return m_nextId++;
    }

    int IndexOfId(uint32_t id) const
    {
        for (int i = 0; i < (int)keys.size(); ++i)
            if (keys[i].id == id) return i;
        return -1;
    }

    void SortByTime()
    {
        std::stable_sort(keys.begin(), keys.end(),
            [](const CurveKeyframe &a, const CurveKeyframe &b) { return a.time < b.time; });
    }

    // キー追加。追加後のインデックスを返す
    int AddKey(float t, float v)
    {
        CurveKeyframe k;
        k.time  = t;
        k.value = v;
        k.id    = GenId();
        keys.push_back(k);
        SortByTime();
        RecalculateTangents();
        return IndexOfId(k.id);
    }

    // ロード後などに、ID未採番のキーへ一括でIDを割り当てる
    void AssignMissingIds()
    {
        for (auto &k : keys)
            if (k.id == 0) k.id = GenId();
    }

    // -----------------------------------------------------------------------
    // 接線の再計算（Auto / Flat のキーだけ更新。Aligned / Broken は手動値を尊重）
    // -----------------------------------------------------------------------
    void RecalculateTangents()
    {
        const size_t n = keys.size();
        for (size_t i = 0; i < n; ++i)
        {
            auto &k = keys[i];

            // 前後キーから滑らかな傾きを推定（Catmull-Rom 相当）
            float autoSlope = 0.0f;
            if (n >= 2)
            {
                if (i == 0)
                {
                    float dt  = keys[1].time - keys[0].time;
                    autoSlope = (dt > 0.0f) ? (keys[1].value - keys[0].value) / dt : 0.0f;
                }
                else if (i == n - 1)
                {
                    float dt  = keys[i].time - keys[i - 1].time;
                    autoSlope = (dt > 0.0f) ? (keys[i].value - keys[i - 1].value) / dt : 0.0f;
                }
                else
                {
                    float dt  = keys[i + 1].time - keys[i - 1].time;
                    autoSlope = (dt > 0.0f) ? (keys[i + 1].value - keys[i - 1].value) / dt : 0.0f;
                }
            }

            if (k.tangentIn == TangentMode::Auto)
                k.inTangent = autoSlope;
            else if (k.tangentIn == TangentMode::Flat)
                k.inTangent = 0.0f;

            if (k.tangentOut == TangentMode::Auto)
                k.outTangent = autoSlope;
            else if (k.tangentOut == TangentMode::Flat)
                k.outTangent = 0.0f;
        }
    }

    // -----------------------------------------------------------------------
    // 評価
    // -----------------------------------------------------------------------
    float Evaluate(float t) const
    {
        if (keys.empty()) return 0.0f;
        const CurveKeyframe &first = keys.front();
        const CurveKeyframe &last  = keys.back();
        if (keys.size() == 1) return first.value;

        const float period = last.time - first.time;

        if (t < first.time) return EvaluatePre(t, period);
        if (t > last.time) return EvaluatePost(t, period);

        // 内部区間: t を挟むキー k0,k1 を二分探索
        auto it = std::upper_bound(keys.begin(), keys.end(), t,
            [](float val, const CurveKeyframe &k) { return val < k.time; });
        if (it == keys.begin()) return first.value;
        if (it == keys.end()) return last.value;
        const CurveKeyframe &k1 = *it;
        const CurveKeyframe &k0 = *(it - 1);
        return EvaluateSegment(k0, k1, t);
    }

    // 微分（傾き）。ハンドルの向き計算やLinear外挿に使う
    float FirstSlope() const
    {
        if (keys.size() < 2) return 0.0f;
        float dt = keys[1].time - keys[0].time;
        return (dt > 0.0f) ? (keys[1].value - keys[0].value) / dt : 0.0f;
    }
    float LastSlope() const
    {
        const size_t n = keys.size();
        if (n < 2) return 0.0f;
        float dt = keys[n - 1].time - keys[n - 2].time;
        return (dt > 0.0f) ? (keys[n - 1].value - keys[n - 2].value) / dt : 0.0f;
    }

    // -----------------------------------------------------------------------
    // 範囲（フレーミング用）
    // -----------------------------------------------------------------------
    float MinTime() const
    {
        return keys.empty() ? 0.0f : keys.front().time;
    }
    float MaxTime() const
    {
        return keys.empty() ? 1.0f : keys.back().time;
    }
    void ValueRange(float &outMin, float &outMax) const
    {
        if (keys.empty())
        {
            outMin = 0.0f;
            outMax = 1.0f;
            return;
        }
        outMin = outMax = keys.front().value;
        for (const auto &k : keys)
        {
            outMin = std::min(outMin, k.value);
            outMax = std::max(outMax, k.value);
        }
    }

  private:
    // 1区間のエルミート評価
    static float EvaluateSegment(const CurveKeyframe &k0, const CurveKeyframe &k1, float t)
    {
        const float dt = k1.time - k0.time;
        if (dt <= 0.0f) return k0.value;
        const float u = (t - k0.time) / dt;

        switch (k0.interp)
        {
        case CurveInterp::Constant:
            return k0.value;
        case CurveInterp::Linear:
            return k0.value + (k1.value - k0.value) * u;
        case CurveInterp::Cubic:
        default: {
            const float u2  = u * u;
            const float u3  = u2 * u;
            const float h00 = 2.0f * u3 - 3.0f * u2 + 1.0f;
            const float h10 = u3 - 2.0f * u2 + u;
            const float h01 = -2.0f * u3 + 3.0f * u2;
            const float h11 = u3 - u2;
            const float m0  = k0.outTangent * dt;
            const float m1  = k1.inTangent * dt;
            return h00 * k0.value + h10 * m0 + h01 * k1.value + h11 * m1;
        }
        }
    }

    float EvaluatePre(float t, float period) const
    {
        const CurveKeyframe &first = keys.front();
        switch (preInfinity)
        {
        case ExtrapMode::Constant:
            return first.value;
        case ExtrapMode::Linear:
            return first.value + FirstSlope() * (t - first.time);
        default:
            return (period > 0.0f) ? EvaluateWrapped(t, period, preInfinity) : first.value;
        }
    }

    float EvaluatePost(float t, float period) const
    {
        const CurveKeyframe &last = keys.back();
        switch (postInfinity)
        {
        case ExtrapMode::Constant:
            return last.value;
        case ExtrapMode::Linear:
            return last.value + LastSlope() * (t - last.time);
        default:
            return (period > 0.0f) ? EvaluateWrapped(t, period, postInfinity) : last.value;
        }
    }

    // Cycle / PingPong / CycleOffset の共通処理
    float EvaluateWrapped(float t, float period, ExtrapMode mode) const
    {
        const CurveKeyframe &first = keys.front();
        const CurveKeyframe &last  = keys.back();

        const float rel     = t - first.time;
        const float cyclesF = std::floor(rel / period);
        const int   cycles  = (int)cyclesF;
        float       local   = rel - cyclesF * period; // [0, period)
        float       ct      = first.time + local;

        if (mode == ExtrapMode::PingPong)
        {
            // 奇数周期は折り返す
            const int parity = ((cycles % 2) + 2) % 2;
            if (parity == 1) ct = last.time - local;
            ct = std::min(std::max(ct, first.time), last.time);
            return Evaluate(ct);
        }

        ct         = std::min(std::max(ct, first.time), last.time);
        float base = Evaluate(ct);
        if (mode == ExtrapMode::CycleOffset)
            base += (last.value - first.value) * cyclesF;
        return base;
    }
};

// ===========================================================================
// JSON シリアライズ（エディタの保存形式と一致。ランタイムからも読める）
//   - CurveKeyframe.id はランタイム専用なので保存しない
//   - interp 未指定のキーは旧 AnimationCurve 形式（線形）とみなす → 後方互換
// ===========================================================================
inline void to_json(nlohmann::json &j, const CurveKeyframe &k)
{
    j = nlohmann::json{
        {"time", k.time}, {"value", k.value},
        {"inTangent", k.inTangent}, {"outTangent", k.outTangent},
        {"interp", (int)k.interp},
        {"tangentIn", (int)k.tangentIn}, {"tangentOut", (int)k.tangentOut}};
}
inline void from_json(const nlohmann::json &j, CurveKeyframe &k)
{
    k.time       = j.value("time", 0.0f);
    k.value      = j.value("value", 0.0f);
    k.inTangent  = j.value("inTangent", 0.0f);
    k.outTangent = j.value("outTangent", 0.0f);
    k.interp     = (CurveInterp)j.value("interp", (int)CurveInterp::Cubic);
    k.tangentIn  = (TangentMode)j.value("tangentIn", (int)TangentMode::Auto);
    k.tangentOut = (TangentMode)j.value("tangentOut", (int)TangentMode::Auto);
    k.id         = 0; // ロード後 AssignMissingIds で採番
}

inline void to_json(nlohmann::json &j, const CurveData &c)
{
    j = nlohmann::json{
        {"name", c.name}, {"color", c.color}, {"visible", c.visible},
        {"preInfinity", (int)c.preInfinity}, {"postInfinity", (int)c.postInfinity},
        {"keys", c.keys}};
}
inline void from_json(const nlohmann::json &j, CurveData &c)
{
    c.name         = j.value("name", std::string("Curve"));
    c.color        = j.value("color", 0xFF80FF00u);
    c.visible      = j.value("visible", true);
    c.preInfinity  = (ExtrapMode)j.value("preInfinity", (int)ExtrapMode::Constant);
    c.postInfinity = (ExtrapMode)j.value("postInfinity", (int)ExtrapMode::Constant);
    c.keys.clear();
    if (j.contains("keys") && j["keys"].is_array())
        c.keys = j["keys"].get<std::vector<CurveKeyframe>>();
    c.AssignMissingIds();
    c.RecalculateTangents(); // Auto/Flat の接線を確定（Aligned/Broken の手動値は尊重）
}

// ---------------------------------------------------------------------------
// 実行時アセット用ローダ。
//   エディタは複数カーブ文書 {name, curves:[...]} で保存するため、
//   ランタイムでは「先頭の1本」を採用する（1ファイル=1カーブ運用）。
// ---------------------------------------------------------------------------
inline bool LoadCurveDataFromJson(const nlohmann::json &root, CurveData &out)
{
    if (!root.contains("curves") || !root["curves"].is_array() || root["curves"].empty())
        return false;
    out = root["curves"][0].get<CurveData>();
    return !out.keys.empty();
}