#pragma once
#include "Editor/Core/EditorWindow.h"
#include "Engine/Platform/Core/Logger.h"

#include <memory>
#include <string>

class ImGuiBufferSink;

class ConsoleWindow : public EditorWindow
{
public:
    ConsoleWindow();

    /**
     * @brief ConsoleWindow に表示用シンクを差し込む
     * @details Framework 初期化時に ImGuiBufferSink を共有することで、
     *          UI 側はロック安全な Snapshot() 経由で履歴を読める
     */
    void SetBufferSink(std::shared_ptr<ImGuiBufferSink> sink) { _bufferSink = std::move(sink); }

protected:
    void DrawContents(EditorContext& context) override;

private:
    std::shared_ptr<ImGuiBufferSink> _bufferSink;

    // レベルフィルタ
    bool _showInfo = true;
    bool _showSuccess = true;
    bool _showWarning = true;
    bool _showError = true;

    bool _autoScroll = true;

    // カテゴリフィルタ (LogCategory::Count 個、全 true で初期化)
    bool _categoryFilters[static_cast<int>(LogCategory::Count)] = {};

    // 検索
    char _searchBuf[128] = {};

    // 初期化済みフラグ
    bool _initialized = false;
};