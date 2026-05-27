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
        ::OutputDebugStringA(line.c_str());
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
        : _path(std::move(path)), _maxBytes(maxBytes), _maxRotated(maxRotated)
    {
        std::error_code ec;
        std::filesystem::create_directories(_path.parent_path(), ec);
        Open(true /*truncate*/);
    }

    ~FileSink() override
    {
        if (_stream.is_open())
        {
            _stream << "=== Engine Log Closed ===" << std::endl;
            _stream.close();
        }
    }

    void Write(const LogMessage& msg) override
    {
        std::lock_guard lock(_mutex);
        if (!_stream.is_open()) return;

        const std::string line = LogFormat::Format(msg) + '\n';
        _stream.write(line.data(), static_cast<std::streamsize>(line.size()));
        _bytesWritten += line.size();

        // ローテーション判定
        if (_maxBytes != 0 && _bytesWritten >= _maxBytes)
        {
            Rotate();
        }
    }

    void Flush() override
    {
        std::lock_guard lock(_mutex);
        if (_stream.is_open()) _stream.flush();
    }

private:
    void Open(bool truncate)
    {
        const auto mode = std::ios::out | (truncate ? std::ios::trunc : std::ios::app);
        _stream.open(_path, mode);
        if (_stream.is_open())
        {
            _stream << "=== Engine Log Started ===" << std::endl;
        }
        _bytesWritten = 0;
    }

    /// .log -> .log.1 -> .log.2 -> ... に押し出す
    void Rotate()
    {
        _stream.close();

        // 古いものから順に番号を上げる ( .log.(N-1) を .log.N にリネーム )
        for (int i = _maxRotated; i >= 1; --i)
        {
            const auto src = (i == 1) ? _path : RotatedPath(i - 1);
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
        auto p = _path;
        p += "." + std::to_string(index);
        return p;
    }

    std::filesystem::path _path;
    std::ofstream         _stream;
    std::mutex            _mutex;
    uint64_t              _bytesWritten = 0;
    uint64_t              _maxBytes;
    int                   _maxRotated;
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
    explicit ImGuiBufferSink(size_t capacity = 1000) : _capacity(capacity) {}

    void Write(const LogMessage& msg) override
    {
        std::lock_guard lock(_mutex);
        if (_buffer.size() >= _capacity)
        {
            _buffer.pop_front();
        }
        _buffer.push_back(msg);
    }

    void Clear() override
    {
        std::lock_guard lock(_mutex);
        _buffer.clear();
    }

    /// ConsoleWindow 用: ロック内でコピーして返す (UI側はロックを長く持たない)
    std::vector<LogMessage> Snapshot() const
    {
        std::lock_guard lock(_mutex);
        return std::vector<LogMessage>(_buffer.begin(), _buffer.end());
    }

    size_t Size() const
    {
        std::lock_guard lock(_mutex);
        return _buffer.size();
    }

private:
    mutable std::mutex     _mutex;
    std::deque<LogMessage> _buffer;
    size_t                 _capacity;
};