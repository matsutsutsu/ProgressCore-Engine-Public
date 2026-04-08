#include "Logger.h"
#include <cstdarg>
#include <cstdio>
#include <Windows.h>

void Logger::Log(LogLevel level, const char* fmt, ...)
{
    // 1. フォーマット文字列の展開 (printfと同じ処理)
    char buffer[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    // Visual Studioの「出力」ウィンドウにも同時に流す（超便利）
    OutputDebugStringA(buffer);
    OutputDebugStringA("\n");

    // 2. スレッドセーフに配列へ追加
    std::lock_guard<std::mutex> lock(_mutex);

    _messages.push_back({ level, std::string(buffer) });

    // 古いログから忘れる（上限を超えたら先頭を消す）
    if (_messages.size() > MAX_LOG_COUNT) {
        _messages.erase(_messages.begin());
    }
}

void Logger::Clear()
{
    std::lock_guard<std::mutex> lock(_mutex);
    _messages.clear();
}