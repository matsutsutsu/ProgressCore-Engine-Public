#pragma once
//==============================================================================
// LogSinks.h
//
// 標準シンク3種:
//   - DebugStreamSink : Visual Studio の「出力」ウィンドウ向け
//   - FileSink        : ログファイル (ローテーション機能つき)
//   - ImGuiBufferSink : ConsoleWindow から読み出す用のリングバッファ
//==============================================================================

#include "Logger.h"

#include <Windows.h>
#include <chrono>
#include <ctime>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <vector>

//==============================================================================
// 共通フォーマット - ファイル/Debug出力の共通整形
//==============================================================================
namespace LogFormat
{
    inline const char* LevelLabel(LogLevel level) noexcept
    {
        switch (level)
        {
        case LogLevel::Error:   return "ERROR";
        case LogLevel::Warning: return "WARN";
        case LogLevel::Success: return "SUCCESS";
        case LogLevel::Info:
        default:                return "INFO";
        }
    }

    /// 整形済みの1行ログを生成 (タイムスタンプ含む)
    inline std::string Format(const LogMessage& msg)
    {
        using namespace std::chrono;
        const auto t = system_clock::to_time_t(msg.time);
        const auto sub = duration_cast<milliseconds>(msg.time.time_since_epoch()).count() % 1000;

        std::tm local{};
#ifdef _WIN32
        localtime_s(&local, &t);
#else
        local = *std::localtime(&t);
#endif

        std::ostringstream os;
        os << std::put_time(&local, "%H:%M:%S")
            << '.' << std::setfill('0') << std::setw(3) << sub << ' '
            << '[' << LevelLabel(msg.level) << "] "
            << '[' << GetLogCategoryName(msg.category) << "] "
            << '[' << msg.file << ':' << msg.line << "] "
            << msg.text;
        return os.str();
    }
}

//==============================================================================
// DebugStreamSink
//   Visual Studio の「出力」ウィンドウ (OutputDebugString)
//==============================================================================
class DebugStreamSink : public ILogSink
{
public:
    void Write(const LogMessage& msg) override
    {
        const std::string line = LogFormat::Format(msg) + '\n';

        // UTF-8 → UTF-16 変換
        int len = MultiByteToWideChar(CP_UTF8, 0, line.c_str(), -1, nullptr, 0);
        if (len <= 0) return;

        std::wstring wline(len, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, line.c_str(), -1, wline.data(), len);

        // Wide 文字でデバッグ出力
        ::OutputDebugStringW(wline.c_str());
    }
};


//==============================================================================
// FileSink
//   ファイル出力 + サイズローテーション
//
//   使い方: std::make_shared<FileSink>("Build/Logs/Engine.log", 10 * 1024 * 1024, 3);
//     -> 10MB を超えたら Engine.log.1, .2, .3 へローテーション
//==============================================================================
class FileSink : public ILogSink
{
public:
    /**
     * @param path           出力先パス
     * @param maxBytes       1ファイルの上限バイト数 (0 ならローテーションしない)
     * @param maxRotated     保持する旧ファイル数 (.1 .2 .3...)
     */
    FileSink(std::filesystem::path path,
        uint64_t maxBytes = 10ull * 1024 * 1024,
        int      maxRotated = 3)
        : m_path(std::move(path)), m_maxBytes(maxBytes), m_maxRotated(maxRotated)
    {
        // ★ ここではファイルを開かない（遅延生成）。
        //   最初のログが実際に書き込まれるまでフォルダ/ファイルを作らないため、
        //   Shipping のようにログが ERROR のみの構成だと「1件も出なければ何も残らない」。
        //   これで正常終了時にログフォルダで散らからず、問題が起きた時だけ痕跡が残る。
    }

    ~FileSink() override
    {
        if (m_stream.is_open())
        {
            m_stream << "=== Engine Log Closed ===" << std::endl;
            m_stream.close();
        }
    }

    void Write(const LogMessage& msg) override
    {
        std::lock_guard lock(m_mutex);

        // 遅延生成：最初の書き込みでフォルダ作成＋ファイルオープン。
        if (!m_opened)
        {
            std::error_code ec;
            if (m_path.has_parent_path())
                std::filesystem::create_directories(m_path.parent_path(), ec);
            Open(true /*truncate*/);
            m_opened = true;
        }
        if (!m_stream.is_open()) return;

        const std::string line = LogFormat::Format(msg) + '\n';
        m_stream.write(line.data(), static_cast<std::streamsize>(line.size()));
        m_bytesWritten += line.size();

        // ローテーション判定
        if (m_maxBytes != 0 && m_bytesWritten >= m_maxBytes)
        {
            Rotate();
        }
    }

    void Flush() override
    {
        std::lock_guard lock(m_mutex);
        if (m_stream.is_open()) m_stream.flush();
    }

private:
    void Open(bool truncate)
    {
        const auto mode = std::ios::out | (truncate ? std::ios::trunc : std::ios::app);
        m_stream.open(m_path, mode);
        if (m_stream.is_open())
        {
            m_stream << "=== Engine Log Started ===" << std::endl;
        }
        m_bytesWritten = 0;
    }

    /// .log -> .log.1 -> .log.2 -> ... に押し出す
    void Rotate()
    {
        m_stream.close();

        // 古いものから順に番号を上げる ( .log.(N-1) を .log.N にリネーム )
        for (int i = m_maxRotated; i >= 1; --i)
        {
            const auto src = (i == 1) ? m_path : RotatedPath(i - 1);
            const auto dst = RotatedPath(i);
            std::error_code ec;
            if (std::filesystem::exists(src, ec))
            {
                std::filesystem::remove(dst, ec);
                std::filesystem::rename(src, dst, ec);
            }
        }
        Open(true);
    }

    std::filesystem::path RotatedPath(int index) const
    {
        auto p = m_path;
        p += "." + std::to_string(index);
        return p;
    }

    std::filesystem::path m_path;
    std::ofstream         m_stream;
    std::mutex            m_mutex;
    uint64_t              m_bytesWritten = 0;
    uint64_t              m_maxBytes;
    int                   m_maxRotated;
    bool                  m_opened = false; ///< 遅延生成済みか（最初の Write で true）
};

//==============================================================================
// ImGuiBufferSink
//   ConsoleWindow が参照する履歴バッファ。
//   - std::deque でリングバッファ化 (O(1) 上限管理)
//   - Snapshot() でコピーを返すのでロック競合を最小化
//==============================================================================
class ImGuiBufferSink : public ILogSink
{
public:
    explicit ImGuiBufferSink(size_t capacity = 1000) : m_capacity(capacity) {}

    void Write(const LogMessage& msg) override
    {
        std::lock_guard lock(m_mutex);
        if (m_buffer.size() >= m_capacity)
        {
            m_buffer.pop_front();
        }
        m_buffer.push_back(msg);
    }

    void Clear() override
    {
        std::lock_guard lock(m_mutex);
        m_buffer.clear();
    }

    /// ConsoleWindow 用: ロック内でコピーして返す (UI側はロックを長く持たない)
    std::vector<LogMessage> Snapshot() const
    {
        std::lock_guard lock(m_mutex);
        return std::vector<LogMessage>(m_buffer.begin(), m_buffer.end());
    }

    size_t Size() const
    {
        std::lock_guard lock(m_mutex);
        return m_buffer.size();
    }

private:
    mutable std::mutex     m_mutex;
    std::deque<LogMessage> m_buffer;
    size_t                 m_capacity;
};