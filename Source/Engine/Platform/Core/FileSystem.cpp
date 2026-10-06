#include "FileSystem.h"
#include "Engine/Platform/Core/Logger.h"
#include <fstream>
#include <algorithm>
#include <cctype>
#include <system_error>
#include <sstream>

#include <chrono>
#include <cstdio>
#include <tracy/Tracy.hpp>

// ---------------------------------------------------------------------------
// ランタイム中のディスクアクセスを1箇所で可視化する。
//
// ゲーム中に突然 100ms 級のフレーム落ちが出たとき、犯人がファイル読みなら
// 必ずここを通る（プレハブ / AnimSequence / カーブ / 設定 / モデル …）。
// 呼び出し元ごとにゾーンを仕込んで回らなくても、この2関数を押さえれば
// 「いつ・どのファイルが・何ms」が Tracy に残る。
//
//  ・ゾーンにファイルパスを ZoneText で貼る → タイムライン上でゾーンを選べば読める
//  ・閾値を超えた読み込みは TracyMessage も出す → メッセージ一覧から探せる
//    （タイムラインを目視で探し回らなくて済む。まれにしか出ないスパイク向け）
// ---------------------------------------------------------------------------
namespace
{
    // これを超えたら「フレーム落ちの原因になりうる読み込み」として記録する。
    constexpr double kSlowFileReadMs = 2.0;

    /**
     * @brief 遅い読み込みだけ Tracy のメッセージ一覧へ流す。
     * @param virtualPath 読み込んだファイルのパス。
     * @param elapsedMs   読み込みにかかった時間（ミリ秒）。
     * @param bytes       読み込んだ大きさ（バイト）。
     * @note ファイルパスの添付（ZoneText）はここでは行えない。
     *       ZoneText は同一スコープのゾーン変数（___tracy_scoped_zone）を参照するマクロなので、必ず呼び出し側に置く。
     */
    void ReportSlowFileRead(const std::string &virtualPath, double elapsedMs, size_t bytes)
    {
#ifdef TRACY_ENABLE
        if (elapsedMs >= kSlowFileReadMs)
        {
            char      msg[512];
            const int len = std::snprintf(msg, sizeof(msg),
                                          "[SLOW FILE READ] %.2f ms / %zu bytes : %s",
                                          elapsedMs, bytes, virtualPath.c_str());
            if (len > 0)
                TracyMessage(msg, static_cast<size_t>(len));
        }
#else
        (void)virtualPath;
        (void)elapsedMs;
        (void)bytes;
#endif
    }
} // namespace

// ---------------------------------------------------------------------------
// パスと一覧
// ---------------------------------------------------------------------------

void FileSystem::Initialize()
{
    CCL_LOG_INFO(LogCategory::Core, "[FileSystem] Initialized.");
}

std::filesystem::path FileSystem::ResolvePhysicalPath(const std::string &virtualPath)
{
    // 現在は仮想パスをそのまま物理パスとして使う（作業ディレクトリ基準）
    return std::filesystem::path(virtualPath);
}

bool FileSystem::Exists(const std::string &virtualPath)
{
    return std::filesystem::exists(ResolvePhysicalPath(virtualPath));
}

std::vector<std::string> FileSystem::ListFiles(const std::string &virtualDir,
                                               const std::string &extension)
{
    std::vector<std::string> result;

    const auto dir = ResolvePhysicalPath(virtualDir);

    // ディレクトリが無いのは異常ではない（その種類のアセットをまだ作っていないだけ）。
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec)) return result;

    for (const auto &entry : std::filesystem::directory_iterator(dir, ec))
    {
        if (!entry.is_regular_file()) continue;

        if (!extension.empty())
        {
            // 拡張子の大小は OS によって揺れる。呼び出し側に小文字を強制させず、ここで畳む。
            std::string ext = entry.path().extension().string();
            for (char &c : ext)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (ext != extension) continue;
        }

        // 物理パスではなく仮想パスで返す。呼び出し側がそのまま ReadAllText へ渡せる。
        result.push_back(virtualDir + "/" + entry.path().filename().string());
    }

    // ファイルシステムの列挙順は保証がない。並びが実行のたびに変わると、
    // 「どのアセットが先に登録されたか」に依存する不具合が再現しなくなる。
    std::sort(result.begin(), result.end());
    return result;
}

// ---------------------------------------------------------------------------
// 読み書き
// ---------------------------------------------------------------------------

std::string FileSystem::ReadAllText(const std::string &virtualPath)
{
    ZoneScopedN("FileSystem::ReadAllText");
    ZoneText(virtualPath.c_str(), virtualPath.size()); // どのファイルかをゾーンに貼る

    const auto t0 = std::chrono::steady_clock::now();

    auto physicalPath = ResolvePhysicalPath(virtualPath);

    std::ifstream file(physicalPath, std::ios::in | std::ios::binary);
    if (!file.is_open())
    {
        CCL_LOG_ERROR(LogCategory::Core, "[FileSystem] Failed to open text file: %s", virtualPath.c_str());
        return "";
    }

    // ストリームバッファ全体を一度に文字列へ読み込む
    std::ostringstream ss;
    ss << file.rdbuf();
    std::string result = ss.str();

    const std::chrono::duration<double, std::milli> elapsed =
        std::chrono::steady_clock::now() - t0;
    ReportSlowFileRead(virtualPath, elapsed.count(), result.size());

    return result;
}

bool FileSystem::WriteAllText(const std::string &virtualPath, const std::string &content)
{
#ifdef SHIPPING_BUILD
    // 製品版で Assets フォルダを上書きしようとするのは、通常はバグ
    CCL_LOG_WARN(LogCategory::Core, "[FileSystem] Attempting to write file in Shipping build: %s", virtualPath.c_str());
#endif

    auto physicalPath = ResolvePhysicalPath(virtualPath);

    // フォルダが存在しない場合は作成する
    if (physicalPath.has_parent_path())
    {
        std::filesystem::create_directories(physicalPath.parent_path());
    }

    std::ofstream file(physicalPath, std::ios::out | std::ios::trunc | std::ios::binary);
    if (!file.is_open())
    {
        CCL_LOG_ERROR(LogCategory::Core, "[FileSystem] Failed to write text file: %s", virtualPath.c_str());
        return false;
    }

    file.write(content.data(), content.size());
    file.flush();

    // write/flush の成否を確認する。見ないと、ディスク不足などで途中までしか
    // 書けていなくても「保存できた」と報告してしまう。
    if (!file)
    {
        CCL_LOG_ERROR(LogCategory::Core, "[FileSystem] Write failed (disk full?): %s", virtualPath.c_str());
        return false;
    }
    return true;
}

std::vector<uint8_t> FileSystem::ReadAllBytes(const std::string &virtualPath)
{
    ZoneScopedN("FileSystem::ReadAllBytes");
    ZoneText(virtualPath.c_str(), virtualPath.size()); // どのファイルかをゾーンに貼る

    const auto t0 = std::chrono::steady_clock::now();

    auto physicalPath = ResolvePhysicalPath(virtualPath);

    std::ifstream file(physicalPath, std::ios::in | std::ios::binary | std::ios::ate);
    if (!file.is_open())
    {
        CCL_LOG_ERROR(LogCategory::Core, "[FileSystem] Failed to open binary file: %s", virtualPath.c_str());
        return {};
    }

    // ファイルサイズを取得して一括確保
    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> buffer(size);
    if (file.read(reinterpret_cast<char *>(buffer.data()), size))
    {
        const std::chrono::duration<double, std::milli> elapsed =
            std::chrono::steady_clock::now() - t0;
        ReportSlowFileRead(virtualPath, elapsed.count(), buffer.size());
        return buffer;
    }

    return {};
}