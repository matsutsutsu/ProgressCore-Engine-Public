// ===================================================================================
// ファイル名: MemoryAnalyzerWindow.h
// 役割: ResourceManagerの連続メモリプールからアセットの消費容量を秒間60フレームを
//       阻害せずに抽出し、視覚的な予算管理（Budget）を行うエディタウィンドウ。
// ===================================================================================
#pragma once
#include "Editor/Core/EditorWindow.h"
#include <vector>
#include <string>

class MemoryAnalyzerWindow : public EditorWindow
{
public:
    MemoryAnalyzerWindow();
    virtual ~MemoryAnalyzerWindow() = default;

protected:
    // エディタマネージャーから毎フレーム呼ばれる描画関数
    void DrawContents(EditorContext& context) override;

private:
    // 毎フレームの集計負荷をゼロにするためのキャッシュ構造体
    struct CachedAssetInfo {
        std::string path;
        size_t sizeBytes = 0;
        uint32_t width = 0;
        uint32_t height = 0;
    };

    // UI側のキャッシュデータ
    std::vector<CachedAssetInfo> m_cachedTextures;
    size_t m_cachedTotalVram = 0;
    float  m_updateTimer = 0.0f;

    // VRAM予算（例として2.0GBに設定。プロジェクトの規模に応じて調整可能）
    const size_t VRAM_BUDGET_BYTES = 2ULL * 1024 * 1024 * 1024;

    // 内部レイアウト描画ヘルパー
    void DrawOverviewSection();
    void DrawTextureTableSection();
};