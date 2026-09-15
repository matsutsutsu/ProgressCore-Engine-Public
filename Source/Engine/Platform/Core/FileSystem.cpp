#include "FileSystem.h"
#include "Engine/Platform/Core/Logger.h" // ログ機能 (CCL_LOG等) に合わせて変更してください
#include <fstream>
#include <algorithm>
#include <cctype>
#include <system_error>
#include <sstream>

#include <chrono>
#include <cstdio>
#include <tracy/Tracy.hpp>

// ===========================================================================
// ランタイム中のディスクアクセスを1箇所で可視化する。
//
// ゲーム中に突然 100ms 級のフレーム落ちが出たとき、犯人がファイル読みなら
// 必ずここを通る（プレハブ / AnimSequence / カーブ / 設定 / モデル …）。
// 呼び出し元ごとにゾーンを仕込んで回らなくても、この2関数を押さえれば
// 「いつ・どのファイルが・何ms」が Tracy に残る。
//
//  ・ゾーンにファイルパスを ZoneText で貼る → タイムライン上でゾーンを掴めば読める
//  ・閾値を超えた読み込みは TracyMessage も出す → メッセージ一覧から一発で探せる
//    （タイムラインを目視で探し回らなくて済む。稀にしか出ないスパイク向け）
// ===========================================================================
namespace
{
// これを超えたら「フレーム落ちの原因になりうる読み込み」として記録する。
constexpr double kSlowFileReadMs = 2.0;

// 遅い読み込みだけメッセージ一覧へ流す。
//   ※ ファイルパスの添付(ZoneText)はここでは行えない。ZoneText は同一スコープの
//     ゾーン変数(___tracy_scoped_zone)を参照するマクロなので、必ず呼び出し側に置く。
void ReportSlowFileRead(const std::string &virtualPath, double elapsedMs, size_t bytes)
{
#ifdef TRACY_ENABLE
    if (elapsedMs >= kSlowFileReadMs)
    {
        char msg[512];
        const int len = std::snprintf(msg, sizeof(msg),
            "[SLOW FILE READ] %.2f ms / %zu bytes : %s",
            elapsedMs, bytes, virtualPath.c_str());
        if (len > 0)
            TracyMessage(msg, static_cast<size_t>(len));
    }
#else
    (void)virtualPath; (void)elapsedMs; (void)bytes;
#endif
}
} // namespace

void FileSystem::Initialize()
{
    // 将来的にはここで
    // #ifdef SHIPPING_BUILD
    // MountPak("Data.pak", "パスワード");
    // #endif
    // のような処理が入る
    CCL_LOG_INFO(LogCategory::Core, "[FileSystem] Initialized.");
}

std::filesystem::path FileSystem::ResolvePhysicalPath(const std::string& virtualPath)
{
    // 現在は Assets フォルダを直接見に行くだけ。
    // 将来的に「セーブデータは AppData 配下へ」「アセットは .pak 内へ」といった
    // ルーティングをここで行う。
    return std::filesystem::path(virtualPath);
}

bool FileSystem::Exists(const std::string& virtualPath)
{
    // 将来的には .pak 内の目次 (Trie木など) を検索する処理に分岐する
    return std::filesystem::exists(ResolvePhysicalPath(virtualPath));
}

std::vector<std::string> FileSystem::ListFiles(const std::string& virtualDir,
                                               const std::string& extension)
{
    std::vector<std::string> result;

    const auto dir = ResolvePhysicalPath(virtualDir);

    // ディレクトリが無いのは異常ではない（その種類のアセットをまだ作っていないだけ）。
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec)) return result;

    for (const auto& entry : std::filesystem::directory_iterator(dir, ec))
    {
        if (!entry.is_regular_file()) continue;

        if (!extension.empty())
        {
            // 拡張子の大小は OS によって揺れる。呼び出し側に小文字を強制させず、ここで畳む。
            std::string ext = entry.path().extension().string();
            for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
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

std::string FileSystem::ReadAllText(const std::string& virtualPath)
{
    // TODO: Shipping構成時は .pak から展開したメモリブロックを std::string にして返す処理に分岐
    ZoneScopedN("FileSystem::ReadAllText");
    ZoneText(virtualPath.c_str(), virtualPath.size()); // どのファイルかをゾーンに貼る

    const auto t0 = std::chrono::steady_clock::now();

    auto physicalPath = ResolvePhysicalPath(virtualPath);

    std::ifstream file(physicalPath, std::ios::in | std::ios::binary);
    if (!file.is_open()) {
        CCL_LOG_ERROR(LogCategory::Core, "[FileSystem] Failed to open text file: %s", virtualPath.c_str());
        return "";
    }

    // ストリームバッファ全体を一気に文字列として読み込む高速な手法
    std::ostringstream ss;
    ss << file.rdbuf();
    std::string result = ss.str();

    const std::chrono::duration<double, std::milli> elapsed =
        std::chrono::steady_clock::now() - t0;
    ReportSlowFileRead(virtualPath, elapsed.count(), result.size());

    return result;
}

bool FileSystem::WriteAllText(const std::string& virtualPath, const std::string& content)
{
#ifdef SHIPPING_BUILD
    // 製品版で Assets フォルダを上書きしようとするのは通常バグか不正アクセス
    // セーブデータの場合は SaveData フォルダなどに分けるルーティングが必要
    CCL_LOG_WARN(LogCategory::System, "[FileSystem] Attempting to write file in Shipping build: %s", virtualPath.c_str());
#endif

    auto physicalPath = ResolvePhysicalPath(virtualPath);

    // フォルダが存在しない場合は作成する
    if (physicalPath.has_parent_path()) {
        std::filesystem::create_directories(physicalPath.parent_path());
    }

    std::ofstream file(physicalPath, std::ios::out | std::ios::trunc | std::ios::binary);
    if (!file.is_open()) {
        CCL_LOG_ERROR(LogCategory::Core, "[FileSystem] Failed to write text file: %s", virtualPath.c_str());
        return false;
    }

    file.write(content.data(), content.size());
    file.flush();

    // write/flush の成否を見ずに true を返していた。ディスク不足などで
    // 途中までしか書けていなくても「保存できた」と報告されてしまう。
    if (!file) {
        CCL_LOG_ERROR(LogCategory::Core, "[FileSystem] Write failed (disk full?): %s", virtualPath.c_str());
        return false;
    }
    return true;
}

std::vector<uint8_t> FileSystem::ReadAllBytes(const std::string& virtualPath)
{
    // TODO: Shipping構成時は .pak から解凍したバイト配列を返す処理に分岐
    ZoneScopedN("FileSystem::ReadAllBytes");
    ZoneText(virtualPath.c_str(), virtualPath.size()); // どのファイルかをゾーンに貼る

    const auto t0 = std::chrono::steady_clock::now();

    auto physicalPath = ResolvePhysicalPath(virtualPath);

    std::ifstream file(physicalPath, std::ios::in | std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        CCL_LOG_ERROR(LogCategory::Core, "[FileSystem] Failed to open binary file: %s", virtualPath.c_str());
        return {};
    }

    // ファイルサイズを取得して一括確保
    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> buffer(size);
    if (file.read(reinterpret_cast<char*>(buffer.data()), size)) {
        const std::chrono::duration<double, std::milli> elapsed =
            std::chrono::steady_clock::now() - t0;
        ReportSlowFileRead(virtualPath, elapsed.count(), buffer.size());
        return buffer;
    }

    return {};
}