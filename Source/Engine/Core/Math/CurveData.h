#pragma once
// ===========================================================================
// 概要: カーブ（キーフレーム列）のデータモデル CurveData と、その評価・JSON 変換
//
// 責務:
//   - キーの追加・並べ替え・接線（自動・水平）の再計算
//   - 任意時刻の評価（区間は線形か 3 次エルミート。キーの範囲外は端の値）
//   - JSON との相互変換
//
// 備考: エディタ非依存なので、ランタイムからも直接使える
// ===========================================================================
#include <vector>
#include <string>
#include <algorithm>
#include <cstdint>
#include <json.hpp>

// キーの右側の区間をどう結ぶか。数値は保存形式に合わせている（0 は廃止したステップ補間）
enum class CurveInterp : uint8_t
{
    Linear = 1, // 直線
    Cubic  = 2, // 接線でなめらかに結ぶ
};

// 3 次の区間で使う接線の決め方
enum class TangentMode : uint8_t
{
    Auto   = 0, // 前後のキーから求める
    Flat   = 1, // 水平（傾き 0）
    Manual = 2, // ハンドルで決めた傾きを保つ
};

struct CurveKeyframe
{
    float       time       = 0.0f;
    float       value      = 0.0f;
    float       inTangent  = 0.0f; // 左側の傾き dy/dx
    float       outTangent = 0.0f; // 右側の傾き dy/dx
    CurveInterp interp     = CurveInterp::Cubic;
    TangentMode tangent    = TangentMode::Auto;

    uint32_t id = 0; // 並べ替えても選択を追うための番号。実行中だけ使い、保存しない
};

struct CurveData
{
    std::string                name    = "Curve";
    uint32_t                   color   = 0xFF80FF00u; // IM_COL32 と同じ並び（0xAABBGGRR）
    bool                       visible = true;
    std::vector<CurveKeyframe> keys;

    uint32_t m_nextId = 1; // キーの id の採番



    bool empty() const { return keys.empty(); }

    uint32_t GenId() { return m_nextId++; }



    // id のキーが keys の何番目かを返す。なければ -1
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



    // キーを追加し、並べ替えた後の添字を返す
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



    // 読み込んだ直後のキーに id を振る
    void AssignMissingIds()
    {
        for (auto &k : keys)
            if (k.id == 0) k.id = GenId();
    }



    // 自動・水平のキーの傾きを決め直す。手動のキーは触らない
    void RecalculateTangents()
    {
        const size_t n = keys.size();
        for (size_t i = 0; i < n; ++i)
        {
            CurveKeyframe &k = keys[i];
            if (k.tangent == TangentMode::Manual) continue;

            float slope = 0.0f;
            if (k.tangent == TangentMode::Auto && n >= 2)
            {
                // 前後のキーを結んだ傾き。端のキーは隣との傾き
                const size_t a  = (i == 0) ? 0 : i - 1;
                const size_t b  = (i == n - 1) ? n - 1 : i + 1;
                const float  dt = keys[b].time - keys[a].time;
                slope           = (dt > 0.0f) ? (keys[b].value - keys[a].value) / dt : 0.0f;
            }

            k.inTangent  = slope;
            k.outTangent = slope;
        }
    }



    // 時刻 t の値を返す。キーの範囲外は端のキーの値
    float Evaluate(float t) const
    {
        if (keys.empty()) return 0.0f;
        if (t <= keys.front().time) return keys.front().value;
        if (t >= keys.back().time) return keys.back().value;

        // t を挟む 2 つのキーを二分探索する
        auto it = std::upper_bound(keys.begin(), keys.end(), t,
                                   [](float val, const CurveKeyframe &k) { return val < k.time; });
        return EvaluateSegment(*(it - 1), *it, t);
    }



    float MinTime() const { return keys.empty() ? 0.0f : keys.front().time; }
    float MaxTime() const { return keys.empty() ? 1.0f : keys.back().time; }



    // 全キーの値の最小・最大を返す。キーがなければ 0〜1
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
    // 1 区間の評価。結び方は左側のキー（k0）の interp で決まる
    static float EvaluateSegment(const CurveKeyframe &k0, const CurveKeyframe &k1, float t)
    {
        const float dt = k1.time - k0.time;
        if (dt <= 0.0f) return k0.value;
        const float u = (t - k0.time) / dt;

        if (k0.interp == CurveInterp::Linear) return k0.value + (k1.value - k0.value) * u;

        // 3 次エルミート補間。傾き（dy/dx）に区間の長さを掛けて u の空間へ直す
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
};



// ============================================================
// JSON
// ============================================================

inline void to_json(nlohmann::json &j, const CurveKeyframe &k)
{
    j = nlohmann::json{
        {"time", k.time},
        {"value", k.value},
        {"inTangent", k.inTangent},
        {"outTangent", k.outTangent},
        {"interp", (int)k.interp},
        {"tangent", (int)k.tangent}};
}



inline void from_json(const nlohmann::json &j, CurveKeyframe &k)
{
    k.time       = j.value("time", 0.0f);
    k.value      = j.value("value", 0.0f);
    k.inTangent  = j.value("inTangent", 0.0f);
    k.outTangent = j.value("outTangent", 0.0f);
    k.interp     = (j.value("interp", (int)CurveInterp::Cubic) == (int)CurveInterp::Linear) ? CurveInterp::Linear
                                                                                             : CurveInterp::Cubic;

    // 旧形式は入と出で別のモード（tangentIn / tangentOut）を持ち、手動に 2 と 3 の 2 種類があった
    const int mode = j.contains("tangent") ? j.value("tangent", 0) : j.value("tangentIn", 0);
    k.tangent      = (mode >= (int)TangentMode::Manual) ? TangentMode::Manual : (TangentMode)mode;
    k.id           = 0; // 読み込んだ後に AssignMissingIds で振る
}



inline void to_json(nlohmann::json &j, const CurveData &c)
{
    j = nlohmann::json{{"name", c.name}, {"color", c.color}, {"visible", c.visible}, {"keys", c.keys}};
}



inline void from_json(const nlohmann::json &j, CurveData &c)
{
    c.name    = j.value("name", std::string("Curve"));
    c.color   = j.value("color", 0xFF80FF00u);
    c.visible = j.value("visible", true);
    c.keys.clear();
    if (j.contains("keys") && j["keys"].is_array())
        c.keys = j["keys"].get<std::vector<CurveKeyframe>>();
    c.AssignMissingIds();
    c.RecalculateTangents();
}
