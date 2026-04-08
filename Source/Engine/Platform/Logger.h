#pragma once
#include <string>
#include <vector>
#include <mutex>

// ログの重要度
enum class LogLevel {
    Info,
    Warning,
    Error
};

// 1件のログデータ
struct LogMessage {
    LogLevel level;
    std::string text;
};

// =========================================================
// ログ管理コア (シングルトン)
// =========================================================
class Logger {
public:
    static Logger& Instance() {
        static Logger instance;
        return instance;
    }

    // フォーマット付きでログを記録する関数 (printfと同じ使い方)
    void Log(LogLevel level, const char* fmt, ...);

    // GUI描画用に蓄積されたログを取得
    const std::vector<LogMessage>& GetMessages() const { return _messages; }

    // ログの消去
    void Clear();

private:
    Logger() = default;

    std::vector<LogMessage> _messages;
    std::mutex _mutex; // 物理スレッドなど別スレッドから呼ばれてもクラッシュしないための鍵

    const size_t MAX_LOG_COUNT = 1000; // メモリ爆発を防ぐ上限
};

// どこからでも一瞬で呼べるようにするための便利なマクロ
#define CCL_LOG_INFO(...)    Logger::Instance().Log(LogLevel::Info, __VA_ARGS__)
#define CCL_LOG_WARN(...)    Logger::Instance().Log(LogLevel::Warning, __VA_ARGS__)
#define CCL_LOG_ERROR(...)   Logger::Instance().Log(LogLevel::Error, __VA_ARGS__)