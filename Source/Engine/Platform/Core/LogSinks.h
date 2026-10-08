#pragma once
// ===========================================================================
// 概要: Logger に登録する標準のログ出力先（シンク）3種。
//
// 責務:
//   - DebugStreamSink: Visual Studio の「出力」ウィンドウへ書く
//   - FileSink: ログファイルへ書く（起動ごとに2世代でローテーション）
//   - ImGuiBufferSink: ConsoleWindow が読む件数上限付きのバッファに貯める
//   - ファイル／出力ウィンドウ向けの1行の書式をそろえる
// ===========================================================================

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

// ============================================================
// 共通フォーマット（ファイル／出力ウィンドウ用の1行）
// ============================================================
namespace LogFormat
{
    inline const char *LevelLabel(LogLevel level) noexcept
    {
        switch (level)
        {
        case LogLevel::Error:
            return "ERROR";
        case LogLevel::Warning:
            return "WARN";
        case LogLevel::Success:
            return "SUCCESS";
        case LogLevel::Info:
        default:
            return "INFO";
        }
    }

    // "時:分:秒.ミリ秒 [レベル] [カテゴリ] [ファイル:行] 本文" の1行を作る
    inline std::string Format(const LogMessage &msg)
    {
        using namespace std::chrono;
        const auto t   = system_clock::to_time_t(msg.time);
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
} // namespace LogFormat

// ============================================================
// DebugStreamSink: Visual Studio の「出力」ウィンドウ（OutputDebugString）
// ============================================================
class DebugStreamSink : public ILogSink
{
public:
    void Write(const LogMessage &msg) override
    {
        const std::string line = LogFormat::Format(msg) + '\n';

        // 日本語を文字化けさせないため、UTF-16 にして W 版で出す
        int len = MultiByteToWideChar(CP_UTF8, 0, line.c_str(), -1, nullptr, 0);
        if (len <= 0) return;

        std::wstring wline(len, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, line.c_str(), -1, wline.data(), len);

        ::OutputDebugStringW(wline.c_str());
    }
};

// ============================================================
// FileSink: ファイル出力（起動ごとに2世代でローテーション）
//
//   使い方: std::make_shared<FileSink>("Build/Logs/Engine.log");
//     起動して最初のログを書くときに、前回起動時の Engine.log が残っていれば
//     Engine.prev.log へ退避（2回前は破棄）してから新規に Engine.log を開く。
//     常に「今回(Engine.log)＋前回(Engine.prev.log)」の2つが残る。
// ============================================================
class FileSink : public ILogSink
{
public:
    /**
     * @param path 出力先パス（例: "Build/Logs/Engine.log"）。
     */
    explicit FileSink(std::filesystem::path path)
        : m_path(std::move(path))
    {
        // ここではファイルを開かない（遅延生成）。
        //   最初のログが実際に書き込まれるまでフォルダ/ファイルを作らないため、
        //   Shipping のようにログが ERROR のみの構成だと「1件も出なければ何も残らない」。
        //   正常時はログフォルダが増えず、問題が起きたときだけ記録が残る。
        //   世代退避も「最初の書き込み時」に行うので、この遅延生成の性質は保たれる。
    }

    ~FileSink() override
    {
        if (m_stream.is_open())
        {
            m_stream << "=== Engine Log Closed ===" << std::endl;
            m_stream.close();
        }
    }

    void Write(const LogMessage &msg) override
    {
        std::lock_guard lock(m_mutex);

        // 遅延生成：最初の書き込みでフォルダ作成＋世代退避＋ファイルオープン。
        if (!m_opened)
        {
            std::error_code ec;
            if (m_path.has_parent_path())
                std::filesystem::create_directories(m_path.parent_path(), ec);

            // 2世代ローテーション：前回起動の Engine.log が残っていれば
            //   Engine.prev.log へ退避してから新規に開く。これで「今回＋前回」を常に保持し、
            //   2回前(既存の prev)は破棄する。1つ前を見返したい時はこの prev を開けばよい。
            if (std::filesystem::exists(m_path, ec))
            {
                const auto prev = PrevPath();
                std::filesystem::remove(prev, ec);         // 2回前は捨てる
                std::filesystem::rename(m_path, prev, ec); // 前回分 → prev へ退避
            }

            Open();
            m_opened = true;
        }
        if (!m_stream.is_open()) return;

        const std::string line = LogFormat::Format(msg) + '\n';
        m_stream.write(line.data(), static_cast<std::streamsize>(line.size()));
    }

    void Flush() override
    {
        std::lock_guard lock(m_mutex);
        if (m_stream.is_open()) m_stream.flush();
    }

private:
    void Open()
    {
        m_stream.open(m_path, std::ios::out | std::ios::trunc);
        if (m_stream.is_open())
        {
            m_stream << "=== Engine Log Started ===" << std::endl;
        }
    }

    // Engine.log -> Engine.prev.log （親フォルダはそのまま、"前回分"の固定名）
    std::filesystem::path PrevPath() const
    {
        auto p = m_path;
        p.replace_filename(m_path.stem().string() + ".prev" + m_path.extension().string());
        return p;
    }

    std::filesystem::path m_path;
    std::ofstream         m_stream;
    std::mutex            m_mutex;
    bool                  m_opened = false; // 遅延生成済みか（最初の Write で true）
};

// ============================================================
// ImGuiBufferSink: ConsoleWindow が読む履歴バッファ
//   - 上限を超えたら古いものから捨てる（std::deque の先頭削除は O(1)）
//   - Snapshot() はコピーを返すので、UI 側がロックを長く持たない
// ============================================================
class ImGuiBufferSink : public ILogSink
{
public:
    explicit ImGuiBufferSink(size_t capacity = 1000) : m_capacity(capacity) {} // capacity: 保持する最大件数

    void Write(const LogMessage &msg) override
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

    // ConsoleWindow 用: ロック内でコピーして返す
    std::vector<LogMessage> Snapshot() const
    {
        std::lock_guard lock(m_mutex);
        return std::vector<LogMessage>(m_buffer.begin(), m_buffer.end());
    }

private:
    mutable std::mutex     m_mutex;
    std::deque<LogMessage> m_buffer;
    size_t                 m_capacity;
};