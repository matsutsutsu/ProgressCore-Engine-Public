#pragma once
#include <cstdint>
#include <string_view>

namespace CCL::Utils {
    // コンパイル時に計算可能なFNV-1aハッシュ関数
    // 文字列を瞬時に単一の整数(32bit)に変換し、検索速度をO(1)かつ最速にします
    constexpr uint32_t HashString(std::string_view str)
    {
        uint32_t hash = 2166136261u;
        for (char c : str) {
            hash ^= static_cast<uint32_t>(c);
            hash *= 16777619u;
        }
        return hash;
    }
} // namespace CCL::Utils