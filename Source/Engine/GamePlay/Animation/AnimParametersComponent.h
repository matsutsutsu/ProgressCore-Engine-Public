#pragma once
#include <vector>
#include <cstdint>

// CPUキャッシュラインに収まりやすいフラットなパラメータボード
struct AnimParametersComponent {
    struct FloatParam { uint32_t hash; float value; };
    struct BoolParam { uint32_t hash; bool value; };

    std::vector<FloatParam> floats;
    std::vector<BoolParam>  bools;

    void SetFloat(uint32_t hash, float val) {
        for (auto& p : floats) {
            if (p.hash == hash) { p.value = val; return; }
        }
        floats.push_back({ hash, val });
    }

    float GetFloat(uint32_t hash) const {
        for (const auto& p : floats) {
            if (p.hash == hash) return p.value;
        }
        return 0.0f; // デフォルト値
    }

    void SetBool(uint32_t hash, bool val) {
        for (auto& p : bools) {
            if (p.hash == hash) { p.value = val; return; }
        }
        bools.push_back({ hash, val });
    }

    bool GetBool(uint32_t hash) const {
        for (const auto& p : bools) {
            if (p.hash == hash) return p.value;
        }
        return false;
    }
};
