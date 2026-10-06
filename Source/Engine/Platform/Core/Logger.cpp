#include "Logger.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace
{
    // フルパスからファイル名のみを抽出する
    const char *ExtractFileName(const char *fullPath) noexcept
    {
        if (!fullPath) return "";
        const char *file = std::strrchr(fullPath, '\\');
        if (!file) file = std::strrchr(fullPath, '/');
        return file ? file + 1 : fullPath;
    }
} // namespace

// ---------------------------------------------------------------------------
// 出力先の登録
// ---------------------------------------------------------------------------

Logger &Logger::Instance()
{
    static Logger instance;
    return instance;
}

Logger::~Logger()
{
    // 終了時に必ずフラッシュ (取りこぼし防止)
    Flush();
}

void Logger::AddSink(std::shared_ptr<ILogSink> sink)
{
    if (!sink) return;
    std::lock_guard lock(m_mutex);
    m_sinks.push_back(std::move(sink));
}

void Logger::RemoveSink(const std::shared_ptr<ILogSink> &sink)
{
    std::lock_guard lock(m_mutex);
    m_sinks.erase(std::remove(m_sinks.begin(), m_sinks.end(), sink), m_sinks.end());
}

void Logger::ClearSinks()
{
    std::lock_guard lock(m_mutex);
    m_sinks.clear();
}

// ---------------------------------------------------------------------------
// ログの出力
// ---------------------------------------------------------------------------

void Logger::Log(LogCategory category, LogLevel level,
                 const std::source_location &loc, const char *fmt, ...)
{
    // 1. 書式を展開する（2048 バイトを超えた分は切り捨て）
    char    buffer[2048];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buffer, sizeof(buffer), fmt ? fmt : "", args);
    va_end(args);

    // 2. LogMessage を組み立てる
    LogMessage msg;
    msg.category = category;
    msg.level    = level;
    msg.time     = std::chrono::system_clock::now();
    msg.threadId = std::this_thread::get_id();
    msg.file     = ExtractFileName(loc.file_name());
    msg.line     = static_cast<int>(loc.line());
    msg.text     = buffer;

    // 3. 各シンクへ配信する
    std::lock_guard lock(m_mutex);
    for (const auto &sink : m_sinks)
    {
        // シンクの例外で他シンクが止まらないようにする
        try
        {
            sink->Write(msg);
        }
        catch (...)
        {
        }
    }

    // 4. Error はすぐフラッシュする（直後にクラッシュしてもファイルに残すため）
    if (level == LogLevel::Error)
    {
        for (const auto &sink : m_sinks)
        {
            try
            {
                sink->Flush();
            }
            catch (...)
            {
            }
        }
    }
}

void Logger::Clear()
{
    std::lock_guard lock(m_mutex);
    for (const auto &sink : m_sinks)
    {
        try
        {
            sink->Clear();
        }
        catch (...)
        {
        }
    }
}

void Logger::Flush()
{
    std::lock_guard lock(m_mutex);
    for (const auto &sink : m_sinks)
    {
        try
        {
            sink->Flush();
        }
        catch (...)
        {
        }
    }
}