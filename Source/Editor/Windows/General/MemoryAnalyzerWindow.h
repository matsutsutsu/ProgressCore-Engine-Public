#pragma once
// ===========================================================================
// 概要: 読み込み済みテクスチャの VRAM 使用量を、予算に対する割合と一覧で表示するウィンドウ
//
// 責務:
//   - ResourceManager のテクスチャプールを 0.2 秒ごとに走査して集計する
//   - 合計と予算（VRAM_BUDGET_BYTES）の比をバーで表示する
//   - テクスチャを使用量の多い順に一覧表示する
//
// 備考: 走査はフレームごとに行わず、0.2 秒ごとの結果をキャッシュして描く
// ===========================================================================
#include "Editor/Core/EditorWindow.h"
#include <vector>
#include <string>

class MemoryAnalyzerWindow : public EditorWindow
{
public:
    MemoryAnalyzerWindow();
    virtual ~MemoryAnalyzerWindow() = default;

protected:
    void DrawContents(EditorContext &context) override;

private:
    // テクスチャ1枚ぶんの集計結果
    struct CachedAssetInfo
    {
        std::string path;
        size_t      sizeBytes = 0;
        uint32_t    width     = 0;
        uint32_t    height    = 0;
    };

    std::vector<CachedAssetInfo> m_cachedTextures;       // 使用量の多い順
    size_t                       m_cachedTotalVram = 0;  // 合計 [byte]
    float                        m_updateTimer     = 0.0f; // 前回の走査からの経過 [秒]

    // VRAM の予算（2GB）。プロジェクトの規模に合わせて変える
    const size_t VRAM_BUDGET_BYTES = 2ULL * 1024 * 1024 * 1024;

    void DrawOverviewSection();     // 合計と予算のバー
    void DrawTextureTableSection(); // テクスチャの一覧
};