#pragma once
// ===========================================================================
// 概要: カテゴリ・レベル付きのログを、登録された出力先（シンク）へ配信するロガー。
//
// 責務:
//   - LogCategory を X-Macro で1か所に定義する（追加は1行で済む）
//   - printf 形式のログに時刻・スレッドID・呼び出し元（source_location）を付ける
//   - 登録されたシンク（LogSinks.h）へ配信し、Error はすぐフラッシュする
//   - CCL_LOG_* マクロの提供（Debug 構成以外では Error 以外を消す）
// ===========================================================================

#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <source_location>
#include <string>
#include <thread>
#include <vector>

// ============================================================
// LogCategory（X-Macro で一元定義。追加は下の LIST に1行足すだけ）
// ============================================================
#define CCL_LOG_CATEGORY_LIST(X) \
    X(Core, "Core")              \
    X(ECS, "ECS")                \
    X(Editor, "Editor")          \
    X(Physics, "Physics")        \
    X(Render, "Render")          \
    X(Audio, "Audio")            \
    X(Game, "Game")              \
    X(AI, "AI")

/**
 * @brief ログのカテゴリ。新しいカテゴリは CCL_LOG_CATEGORY_LIST に追加する
 */
enum class LogCategory : uint8_t
{
#define CCL_X(name, str) name,
    CCL_LOG_CATEGORY_LIST(CCL_X)
#undef CCL_X
    Count
};

/**
 * @brief LogCategory を表示用文字列に変換する
 */
constexpr const char *GetLogCategoryName(LogCategory c) noexcept
{
    constexpr const char *kNames[] = {
#define CCL_X(name, str) str,
        CCL_LOG_CATEGORY_LIST(CCL_X)
#undef CCL_X
    };
    const auto i = static_cast<size_t>(c);
    return (i < static_cast<size_t>(LogCategory::Count)) ? kNames[i] : "Unknown";
}

// ログの重要度
enum class LogLevel : uint8_t
{
    Info,
    Success,
    Warning,
    Error,
};

// ログ1件分のデータ
struct LogMessage
{
    LogCategory                           category = LogCategory::Core;
    LogLevel                              level    = LogLevel::Info;
    std::chrono::system_clock::time_point time{};     // 記録した時刻
    std::thread::id                       threadId{}; // 記録したスレッド
    std::string                           file;       // ファイル名（フルパスではなく名前だけ）
    int                                   line = 0;   // 行番号
    std::string                           text;       // 書式展開済みの本文
};

// ログの出力先の抽象インターフェース。実装は LogSinks.h
class ILogSink
{
public:
    virtual ~ILogSink() = default;

    // ログ1件を受け取り、シンク固有の方法で出力する
    virtual void Write(const LogMessage &msg) = 0;

    // 出力をフラッシュする (ファイルバッファを書き出す等)
    virtual void Flush() {}

    // 履歴をクリアする（履歴を持つ ImGuiBufferSink だけが実装する）
    virtual void Clear() {}
};

// 各シンクへログを配信する。Logger 自身は履歴を持たない。
// UI 表示が必要な場合は ImGuiBufferSink を AddSink して、そのスナップショットを使う。
class Logger
{
public:
    static Logger &Instance();

    // シンクを登録する（起動時に登録する）
    void AddSink(std::shared_ptr<ILogSink> sink);

    // sink の登録を解除する
    void RemoveSink(const std::shared_ptr<ILogSink> &sink);

    // 全シンクを登録解除する
    void ClearSinks();

    /**
     * @brief printf スタイルでログを記録する
     * @param category ログカテゴリ
     * @param level 重要度
     * @param loc 呼び出し元情報（マクロから std::source_location::current() が渡される）
     * @param fmt printf 形式の書式文字列
     */
    void Log(LogCategory category, LogLevel level,
             const std::source_location &loc, const char *fmt, ...);

    // 全シンクに履歴クリアを要求する（ImGuiBufferSink だけが反応する）
    void Clear();

    // 全シンクにフラッシュを要求する
    void Flush();

private:
    Logger() = default;
    ~Logger();
    Logger(const Logger &)            = delete;
    Logger &operator=(const Logger &) = delete;

    mutable std::mutex                     m_mutex;
    std::vector<std::shared_ptr<ILogSink>> m_sinks;
};

// ============================================================
// マクロ
//
// _DEBUG 定義時（Debug 構成）: INFO / SUCCESS / WARN を記録する
// それ以外（Development / Release）: INFO / SUCCESS / WARN は ((void)0) になり、引数も評価されない
// ERROR は全構成で記録する（配布後の不具合を追えるように）
// ============================================================
#ifdef _DEBUG
#define CCL_LOG_INFO(category, ...) \
    Logger::Instance().Log((category), LogLevel::Info, std::source_location::current(), __VA_ARGS__)
#define CCL_LOG_SUCCESS(category, ...) \
    Logger::Instance().Log((category), LogLevel::Success, std::source_location::current(), __VA_ARGS__)
#define CCL_LOG_WARN(category, ...) \
    Logger::Instance().Log((category), LogLevel::Warning, std::source_location::current(), __VA_ARGS__)
#else
#define CCL_LOG_INFO(category, ...) ((void)0)
#define CCL_LOG_SUCCESS(category, ...) ((void)0)
#define CCL_LOG_WARN(category, ...) ((void)0)
#endif

#define CCL_LOG_ERROR(category, ...) \
    Logger::Instance().Log((category), LogLevel::Error, std::source_location::current(), __VA_ARGS__)