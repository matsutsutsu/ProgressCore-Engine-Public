#pragma once
//==============================================================================
// TracySection.h
//
// Tracy v0.14 で追加された「Sections」を RAII（スコープ）で扱う薄いヘルパ。
//   - Section は Zone より一段上の「実行フェーズの帯」。タイムライン上で色付きの
//     帯として重なって表示でき、プロファイラ側で「この帯だけ」に時間範囲を絞って
//     解析できる（例: Simulation フェーズだけを見て ForEachParallel の join 待ちを追う）。
//   - Zone(ZoneScopedN) が「関数の実行時間」なのに対し、Section は「工程」を表す。
//
// 使い方:
//     {
//         TRACY_SECTION("Simulation");   // このスコープ全体が Simulation セクションになる
//         ... FixedUpdate ループ ...
//     }                                   // スコープを抜けると自動で Leave
//
// Shipping（TRACY_ENABLE 未定義）では完全な no-op に落ちる（コード生成ゼロ）。
//==============================================================================

#include "tracy/Tracy.hpp"

#ifdef TRACY_ENABLE

// Section ハンドルをスコープ終端で自動 Leave する RAII ラッパ。
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

// __LINE__ を確実に展開して一意なローカル変数名を作るための2段マクロ。
#define _TRACY_SEC_CONCAT2(a, b) a##b
#define _TRACY_SEC_CONCAT(a, b) _TRACY_SEC_CONCAT2(a, b)

// このスコープを1つの Section にする。name は printf 書式（可変引数可）。
#define TRACY_SECTION(name, ...) \
    TracyScopedSection _TRACY_SEC_CONCAT(_tracySection_, __LINE__)(TracySectionEnter(name, ##__VA_ARGS__))

// カテゴリ付き（category は uint16_t。TracySectionSetup で名前/色を事前登録できる）。
#define TRACY_SECTION_CAT(category, name, ...)                      \
    TracyScopedSection _TRACY_SEC_CONCAT(_tracySection_, __LINE__)( \
        TracySectionEnterCategory(category, name, ##__VA_ARGS__))

#else // TRACY_ENABLE 未定義（Shipping）

#define TRACY_SECTION(name, ...) ((void)0)
#define TRACY_SECTION_CAT(category, name, ...) ((void)0)

#endif
