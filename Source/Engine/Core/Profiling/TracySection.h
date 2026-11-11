#pragma once
// ===========================================================================
// 概要: Tracy v0.14 の Sections を、スコープ（RAII）で扱うヘルパー（TRACY_SECTION / TRACY_SECTION_CAT）
//
// 備考: Section は Zone より一段上の、実行の工程を表す帯。プロファイラでその帯だけに時間範囲を絞って解析できる
//       Shipping（TRACY_ENABLE なし）では何も生成しない
//
// @code
//   {
//       TRACY_SECTION("Simulation"); // このスコープ全体が Simulation の帯になる
//       ... FixedUpdate のループ ...
//   }                                // スコープを抜けると自動で閉じる
// @endcode
// ===========================================================================

#include "tracy/Tracy.hpp"

#ifdef TRACY_ENABLE

// Section ハンドルをスコープ終端で自動 Leave する RAII ラッパ
class TracyScopedSection
{
public:
    explicit TracyScopedSection(uint32_t id) noexcept : m_id(id) {}
    ~TracyScopedSection() { TracySectionLeave(m_id); }

    TracyScopedSection(const TracyScopedSection &)            = delete;
    TracyScopedSection &operator=(const TracyScopedSection &) = delete;

private:
    uint32_t m_id;
};

// __LINE__ を確実に展開して一意なローカル変数名を作るための2段マクロ
#define _TRACY_SEC_CONCAT2(a, b) a##b
#define _TRACY_SEC_CONCAT(a, b) _TRACY_SEC_CONCAT2(a, b)

// このスコープを1つの Section にする。name は printf 書式（可変引数可）
#define TRACY_SECTION(name, ...) \
    TracyScopedSection _TRACY_SEC_CONCAT(_tracySection_, __LINE__)(TracySectionEnter(name, ##__VA_ARGS__))

// カテゴリ付き（category は uint16_t。TracySectionSetup で名前/色を事前登録できる）
#define TRACY_SECTION_CAT(category, name, ...)                      \
    TracyScopedSection _TRACY_SEC_CONCAT(_tracySection_, __LINE__)( \
        TracySectionEnterCategory(category, name, ##__VA_ARGS__))

#else // TRACY_ENABLE 未定義（Shipping）

#define TRACY_SECTION(name, ...) ((void)0)
#define TRACY_SECTION_CAT(category, name, ...) ((void)0)

#endif
