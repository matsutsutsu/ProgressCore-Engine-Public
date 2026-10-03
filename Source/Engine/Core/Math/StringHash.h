#pragma once
// ===========================================================================
// 概要: 文字列をコンパイル時に32bit整数へ変換する FNV-1a ハッシュと、"_hash" リテラル。
//
// 責務:
//   - HashString: 文字列 → 32bit ハッシュ（constexpr）
//   - "abc"_hash: 文字列リテラルをコンパイル時にハッシュ化する
// ===========================================================================
#include <cstdint>
#include <string_view>

namespace CCL::Utils
{
    /**
     * @brief FNV-1a ハッシュ。文字列キーを整数で比較・検索できるようにする。
     *
     * 2166136261 と 16777619 は FNV-1a（32bit）で決められた初期値と素数。
     *
     * @param str ハッシュする文字列。
     * @return 32bit のハッシュ値。
     */
    constexpr uint32_t HashString(std::string_view str)
    {
        uint32_t hash = 2166136261u;
        for (char c : str)
        {
            hash ^= static_cast<uint32_t>(c);
            hash *= 16777619u;
        }
        return hash;
    }
} // namespace CCL::Utils

// 名前空間の外に置くことで、どこでも "abc"_hash と書けるようにする
constexpr uint32_t operator"" _hash(const char *str, size_t)
{
    return CCL::Utils::HashString(str);
}