#pragma once
//==============================================================================
// Logger.h
//
// 販売エンジン向けロガー
//
// 主な特徴:
//   - LogCategory は X-Macro で一元定義 → 追加時の修正は1箇所だけ
//   - Sink (出力先) を差し替え可能 (Debug出力 / ファイル / ImGui / ネットワーク...)
//   - std::source_location で __FILE__/__LINE__ を自動取得
//   - タイムスタンプ + スレッドID を必ず記録
//   - Error は Release でも残し、即フラッシュ
//==============================================================================

#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <source_location>
#include <string>
#include <thread>
#include <vector>

//==============================================================================
// LogCategory - X-Macro で一元定義
//   追加するときは下の LIST に 1行追加するだけ
//==============================================================================
#define CCL_LOG_CATEGORY_LIST(X) \
    X(Core,    "Core")    \
    X(ECS,     "ECS")     \
    X(Editor,  "Editor")  \
    X(Physics, "Physics") \
    X(Render,  "Render")  \
    X(Audio,   "Audio")   \
    X(Game,    "Game")    \
    X(AI,      "AI")

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
constexpr const char* GetLogCategoryName(LogCategory c) noexcept
{
    constexpr const char* kNames[] = {
#define CCL_X(name, str) str,
        CCL_LOG_CATEGORY_LIST(CCL_X)
#undef CCL_X
    };
    const auto i = static_cast<size_t>(c);
    return (i < static_cast<size_t>(LogCategory::Count)) ? kNames[i] : "Unknown";
}

//==============================================================================
// LogLevel
//==============================================================================
enum class LogLevel : uint8_t
{
    Info,
    Success,
    Warning,
    Error,
};

//==============================================================================
// LogMessage - 1件分のログデータ
//==============================================================================
struct LogMessage
{
    LogCategory                           category = LogCategory::Core;
    LogLevel                              level = LogLevel::Info;
    std::chrono::system_clock::time_point time{};
    std::thread::id                       threadId{};
    std::string                           file;       // ファイル名 (フルパスではなく短縮済み)
    int                                   line = 0;
    std::string                           text;
};

//==============================================================================
// ILogSink - 出力先の抽象インターフェース
//
// 実装例: DebugStreamSink (OutputDebugString), FileSink, ImGuiBufferSink, etc
//==============================================================================
class ILogSink
{
public:
    virtual ~ILogSink() = default;

    // ログ1件を受け取り、シンク固有の方法で出力する
    virtual void Write(const LogMessage& msg) = 0;

    // 出力をフラッシュする (ファイルバッファを書き出す等)
    virtual void Flush() {}

    // 履歴をクリアする (ImGuiBufferSink等で使用、不要なシンクは何もしなくて良い)
    virtual void Clear() {}
};

//==============================================================================
// Logger - 中央ハブ
//
// 各シンクへログを配信する。Logger 自身は履歴を持たない。
// UI 表示が必要な場合は ImGuiBufferSink を AddSink して、そのスナップショットを使う。
//==============================================================================
class Logger
{
public:
    // シングルトンアクセス
    static Logger& Instance();

    // シンクを登録する (起動時に推奨)
    void AddSink(std::shared_ptr<ILogSink> sink);

    // シンクを除去する
    void RemoveSink(const std::shared_ptr<ILogSink>& sink);

    // 全シンクをクリアする
    void ClearSinks();

    /**
     * @brief printf スタイルでログを記録する
     * @param category  ログカテゴリ
     * @param level     重要度
     * @param loc       呼び出し元情報 (マクロから std::source_location::current() が渡される)
     * @param fmt       printf 形式の書式文字列
     */
    void Log(LogCategory category, LogLevel level,
        const std::source_location& loc, const char* fmt, ...);

    // 全シンクの履歴クリアを要求 (ImGuiBufferSink等のみ反応)
    void Clear();

    // 全シンクのフラッシュを要求
    void Flush();

private:
    Logger() = default;
    ~Logger();
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    mutable std::mutex                     m_mutex;
    std::vector<std::shared_ptr<ILogSink>> m_sinks;
};

//==============================================================================
// マクロ群
//
// Debug ビルド:   INFO / SUCCESS / WARN は記録される
// Release ビルド: INFO / SUCCESS / WARN は完全に消滅 ((void)0)
// ERROR は常に記録される (販売後のクラッシュ解析の生命線)
//==============================================================================
#ifdef _DEBUG
#define CCL_LOG_INFO(category, ...)    \
        Logger::Instance().Log((category), LogLevel::Info,    std::source_location::current(), __VA_ARGS__)
#define CCL_LOG_SUCCESS(category, ...) \
        Logger::Instance().Log((category), LogLevel::Success, std::source_location::current(), __VA_ARGS__)
#define CCL_LOG_WARN(category, ...)    \
        Logger::Instance().Log((category), LogLevel::Warning, std::source_location::current(), __VA_ARGS__)
#else
#define CCL_LOG_INFO(category, ...)    ((void)0)
#define CCL_LOG_SUCCESS(category, ...) ((void)0)
#define CCL_LOG_WARN(category, ...)    ((void)0)
#endif

#define CCL_LOG_ERROR(category, ...) \
    Logger::Instance().Log((category), LogLevel::Error, std::source_location::current(), __VA_ARGS__)