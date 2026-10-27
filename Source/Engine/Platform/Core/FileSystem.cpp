#include "FileSystem.h"
#include "Engine/Platform/Core/Logger.h"
#include <fstream>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <system_error>
#include <sstream>

#include <chrono>
#include <cstdio>
#include <unordered_map>
#include <unordered_set>
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

    // -----------------------------------------------------------------------
    // アーカイブ（.pak）
    //
    // 書式（Tools/Pak作成ツール/make_pak.py と揃えること。数値はリトルエンディアン）:
    //   [ヘッダ]  "PCPK"(4バイト) / version(u32) / entryCount(u32) / flags(u32)
    //   [目次]    entryCount 回: pathLength(u16) / path(UTF-8) / offset(u64) / size(u64)
    //   [中身]    各ファイルのバイト列。offset はアーカイブの先頭からの位置
    //
    // path は . と .. を解決し、区切りを / にしたもの（大文字小文字は元のまま）。
    // 引くときは小文字にした鍵で探し、一覧を返すときは元の名前を使う
    // （ファイル名から登録名を作るアセットがあるため、名前の大文字小文字は残す）。
    //
    // 暗号化（flags に kArchiveFlagXor が立っているとき）:
    //   ヘッダより後ろ（目次と中身）の全バイトに、乱数列を XOR してある。ヘッダは平文のまま。
    //   乱数列は「アーカイブ先頭からの位置」だけで決まるので、どこから読んでもその場で戻せる。
    //   素材を手軽に取り出せなくするためのもので、鍵は exe に入っているため解析されれば破られる。
    // -----------------------------------------------------------------------

    // Shipping で開くアーカイブ。作業ディレクトリ（普通は exe の隣）に置く
    constexpr const char *kArchivePath = "Data.pak";

    constexpr char     kArchiveMagic[4] = {'P', 'C', 'P', 'K'};
    constexpr uint32_t kArchiveVersion  = 1;
    constexpr uint32_t kArchiveFlagXor  = 1u << 0; // 目次と中身を XOR してある

    // 乱数列の種。make_pak.py の KEY と同じ値にすること
    constexpr uint64_t kArchiveKey = 0x5A3C9E71D24B8F06ull;

    /**
     * @brief 64bit の値をよく混ぜる（splitmix64）。入力が 1 違うだけでも全く違う値になる。
     * @note make_pak.py の splitmix と同じ計算。
     */
    uint64_t SplitMix64(uint64_t x)
    {
        uint64_t z = x + 0x9E3779B97F4A7C15ull;
        z          = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z          = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }

    /**
     * @brief アーカイブの一部を XOR で戻す（同じ処理をもう一度掛けると元に戻る）。
     * @param data     戻すバイト列。その場で書き換える。
     * @param size     data のバイト数。
     * @param position data の先頭が、アーカイブ先頭から何バイト目か。
     * @note 位置 p のバイトには「SplitMix64(鍵 ^ (p / 8)) の p % 8 番目のバイト」を XOR する。
     *       8 バイトごとに違う値を使うので、同じ鍵を繰り返し掛けたときのような模様は出ない。
     */
    void XorArchiveBytes(uint8_t *data, size_t size, uint64_t position)
    {
        size_t i = 0;
        while (i < size)
        {
            const uint64_t p      = position + i;
            const uint64_t stream = SplitMix64(kArchiveKey ^ (p >> 3));
            for (uint64_t byteIndex = p & 7; byteIndex < 8 && i < size; ++byteIndex, ++i)
            {
                data[i] ^= static_cast<uint8_t>(stream >> (byteIndex * 8));
            }
        }
    }

    struct ArchiveEntry
    {
        std::string path;   // 元の名前（正規化済み・大文字小文字はそのまま）
        uint64_t    offset; // アーカイブ先頭からの位置
        uint64_t    size;   // バイト数
    };

    // 目次は Mount で1度だけ作り、以降は読むだけなので、複数スレッドから引いてよい
    std::string                                   g_archivePath;
    std::unordered_map<std::string, ArchiveEntry> g_archiveEntries;          // 鍵は小文字にしたパス
    bool                                          g_archiveEncrypted = false; // 中身を XOR で戻す必要があるか

    /**
     * @brief パスの表記を揃える（\ を / に、. と .. を解決）。
     * @param path 揃えるパス。
     * @param toLower true なら英字を小文字にする（目次を引く鍵に使う）。
     * @note 小文字にするのは ASCII だけ。日本語のファイル名（UTF-8）のバイトは触らない。
     *       std::filesystem::path を通さないのは、UTF-8 の文字列を ANSI として解釈して化けるため。
     */
    std::string NormalizePath(const std::string &path, bool toLower)
    {
        std::vector<std::string> parts;
        std::string              current;

        auto flush = [&]() {
            if (current.empty() || current == ".")
            {
                // 空（// の重なり）と . は読み飛ばす
            }
            else if (current == "..")
            {
                if (!parts.empty() && parts.back() != "..")
                    parts.pop_back();
                else
                    parts.push_back(".."); // 先頭より上を指す .. は残す（アーカイブには当たらない）
            }
            else
            {
                parts.push_back(current);
            }
            current.clear();
        };

        for (char c : path)
        {
            if (c == '/' || c == '\\')
            {
                flush();
                continue;
            }
            if (toLower && c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
            current += c;
        }
        flush();

        std::string result;
        for (const std::string &part : parts)
        {
            if (!result.empty()) result += '/';
            result += part;
        }
        return result;
    }

    const ArchiveEntry *FindArchiveEntry(const std::string &virtualPath)
    {
        if (g_archiveEntries.empty()) return nullptr;

        const auto it = g_archiveEntries.find(NormalizePath(virtualPath, true));
        return (it != g_archiveEntries.end()) ? &it->second : nullptr;
    }

    // アーカイブから1ファイルを読む。毎回開き直すので、複数スレッドから同時に呼んでよい
    bool ReadFromArchive(const ArchiveEntry &entry, std::vector<uint8_t> &out)
    {
        std::ifstream file(g_archivePath, std::ios::in | std::ios::binary);
        if (!file.is_open()) return false;

        out.resize(static_cast<size_t>(entry.size));
        file.seekg(static_cast<std::streamoff>(entry.offset), std::ios::beg);
        if (!file.read(reinterpret_cast<char *>(out.data()), static_cast<std::streamsize>(entry.size)))
            return false;

        if (g_archiveEncrypted) XorArchiveBytes(out.data(), out.size(), entry.offset);
        return true;
    }

    /**
     * @brief 今の位置から size バイト読む。encrypted なら、読んだ位置の乱数列で戻す。
     * @note 位置は読む前に tellg で取る（乱数列は位置で決まるため）。
     */
    bool ReadArchiveBytes(std::ifstream &file, void *out, size_t size, bool encrypted)
    {
        const uint64_t position = static_cast<uint64_t>(file.tellg());
        if (!file.read(static_cast<char *>(out), static_cast<std::streamsize>(size))) return false;

        if (encrypted) XorArchiveBytes(static_cast<uint8_t *>(out), size, position);
        return true;
    }

    template <class T>
    bool ReadValue(std::ifstream &file, T &out, bool encrypted = false)
    {
        return ReadArchiveBytes(file, &out, sizeof(T), encrypted);
    }
} // namespace

// ---------------------------------------------------------------------------
// パスと一覧
// ---------------------------------------------------------------------------

void FileSystem::Initialize()
{
#ifdef SHIPPING_BUILD
    // 開けなくても止めない。ディスクにアセットがあればそのまま動く（開発フォルダから起動したときなど）
    Mount(kArchivePath);
#endif
    CCL_LOG_INFO(LogCategory::Core, "[FileSystem] Initialized.");
}

bool FileSystem::Mount(const std::string &archivePath)
{
    // 処理フロー:
    // 1. ヘッダを確かめる（識別子・版・フラグ）。ヘッダは暗号化されていない
    // 2. 目次を読む（暗号化されていれば、読みながら戻す）
    // 3. 目次の位置と大きさがファイルに収まっているか確かめてから、使い始める

    std::ifstream file(archivePath, std::ios::in | std::ios::binary | std::ios::ate);
    if (!file.is_open())
    {
        CCL_LOG_WARN(LogCategory::Core, "[FileSystem] アーカイブがありません（ディスクから読みます）: %s", archivePath.c_str());
        return false;
    }
    const uint64_t archiveSize = static_cast<uint64_t>(file.tellg());
    file.seekg(0, std::ios::beg);

    // ---- 1. ヘッダ ----
    char     magic[4]   = {};
    uint32_t version    = 0;
    uint32_t entryCount = 0;
    uint32_t flags      = 0;
    if (!file.read(magic, sizeof(magic)) || std::memcmp(magic, kArchiveMagic, sizeof(magic)) != 0 ||
        !ReadValue(file, version) || !ReadValue(file, entryCount) || !ReadValue(file, flags))
    {
        CCL_LOG_ERROR(LogCategory::Core, "[FileSystem] アーカイブではないファイルです: %s", archivePath.c_str());
        return false;
    }
    // 知らないフラグが立っていたら読まない（新しい形式を古いエンジンで誤って読まないように）
    if (version != kArchiveVersion || (flags & ~kArchiveFlagXor) != 0)
    {
        CCL_LOG_ERROR(LogCategory::Core, "[FileSystem] 対応していないアーカイブです (version=%u, flags=%u): %s",
                      version, flags, archivePath.c_str());
        return false;
    }
    const bool encrypted = (flags & kArchiveFlagXor) != 0;

    // ---- 2. 目次 ----
    std::unordered_map<std::string, ArchiveEntry> entries;
    entries.reserve(entryCount);
    for (uint32_t i = 0; i < entryCount; ++i)
    {
        uint16_t     pathLength = 0;
        ArchiveEntry entry;
        if (!ReadValue(file, pathLength, encrypted)) break;
        entry.path.resize(pathLength);
        if (!ReadArchiveBytes(file, entry.path.data(), pathLength, encrypted) ||
            !ReadValue(file, entry.offset, encrypted) || !ReadValue(file, entry.size, encrypted))
            break;

        // ---- 3. 範囲の確認 ----
        // 壊れたアーカイブで、範囲外を読んで巨大な確保をしないように
        if (entry.offset > archiveSize || entry.size > archiveSize - entry.offset)
        {
            CCL_LOG_ERROR(LogCategory::Core, "[FileSystem] アーカイブの目次が壊れています: %s (%s)",
                          archivePath.c_str(), entry.path.c_str());
            return false;
        }
        std::string key = NormalizePath(entry.path, true);
        entries.emplace(std::move(key), std::move(entry));
    }
    if (entries.size() != entryCount)
    {
        CCL_LOG_ERROR(LogCategory::Core, "[FileSystem] アーカイブの目次を読み切れませんでした (%zu / %u): %s",
                      entries.size(), entryCount, archivePath.c_str());
        return false;
    }

    g_archivePath      = archivePath;
    g_archiveEntries   = std::move(entries);
    g_archiveEncrypted = encrypted;
    CCL_LOG_INFO(LogCategory::Core, "[FileSystem] アーカイブを開きました: %s (%u ファイル%s)",
                 archivePath.c_str(), entryCount, encrypted ? "・暗号化あり" : "");
    return true;
}

bool FileSystem::IsInArchive(const std::string &virtualPath)
{
    return FindArchiveEntry(virtualPath) != nullptr;
}

std::filesystem::path FileSystem::ResolvePhysicalPath(const std::string &virtualPath)
{
    // 現在は仮想パスをそのまま物理パスとして使う（作業ディレクトリ基準）
    return std::filesystem::path(virtualPath);
}

bool FileSystem::Exists(const std::string &virtualPath)
{
    if (FindArchiveEntry(virtualPath)) return true;

    // 手入力のパスも渡ってくるので、例外を投げない版を使う（壊れたパスは「無い」扱い）
    std::error_code ec;
    return std::filesystem::exists(ResolvePhysicalPath(virtualPath), ec);
}

std::vector<std::string> FileSystem::ListFiles(const std::string &virtualDir,
                                               const std::string &extension)
{
    std::vector<std::string>        result;
    std::unordered_set<std::string> seen; // アーカイブとディスクの両方にある名前を1つにまとめる（小文字の名前）

    // 拡張子の大小は OS によって揺れる。呼び出し側に小文字を強制させず、ここで畳む。
    auto matchesExtension = [&extension](const std::string &fileName) {
        if (extension.empty()) return true;
        if (fileName.size() < extension.size()) return false;
        std::string tail = fileName.substr(fileName.size() - extension.size());
        for (char &c : tail)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return tail == extension;
    };

    // ---- アーカイブ ----
    // 目次から「このフォルダの直下」にあるものを拾う
    const std::string dirKey = NormalizePath(virtualDir, true) + "/";
    for (const auto &[key, entry] : g_archiveEntries)
    {
        if (key.compare(0, dirKey.size(), dirKey) != 0) continue;
        if (key.find('/', dirKey.size()) != std::string::npos) continue; // さらに下のフォルダの中

        const std::string fileName = entry.path.substr(entry.path.find_last_of('/') + 1); // 元の大文字小文字のまま
        if (!matchesExtension(fileName)) continue;
        if (!seen.insert(key.substr(dirKey.size())).second) continue;

        result.push_back(virtualDir + "/" + fileName);
    }

    // ---- ディスク ----
    const auto dir = ResolvePhysicalPath(virtualDir);

    // ディレクトリが無いのは異常ではない（その種類のアセットをまだ作っていないだけ）。
    std::error_code ec;
    if (std::filesystem::is_directory(dir, ec))
    {
        for (const auto &entry : std::filesystem::directory_iterator(dir, ec))
        {
            if (!entry.is_regular_file()) continue;

            const std::string fileName = entry.path().filename().string();
            if (!matchesExtension(fileName)) continue;
            if (!seen.insert(NormalizePath(fileName, true)).second) continue;

            // 物理パスではなく仮想パスで返す。呼び出し側がそのまま ReadAllText へ渡せる。
            result.push_back(virtualDir + "/" + fileName);
        }
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

    // アーカイブにあればそちらを読む
    if (const ArchiveEntry *entry = FindArchiveEntry(virtualPath))
    {
        std::vector<uint8_t> bytes;
        if (!ReadFromArchive(*entry, bytes))
        {
            CCL_LOG_ERROR(LogCategory::Core, "[FileSystem] Failed to read from archive: %s", virtualPath.c_str());
            return "";
        }
        std::string result(bytes.begin(), bytes.end());

        const std::chrono::duration<double, std::milli> elapsed =
            std::chrono::steady_clock::now() - t0;
        ReportSlowFileRead(virtualPath, elapsed.count(), result.size());
        return result;
    }

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

    // アーカイブにあればそちらを読む
    if (const ArchiveEntry *entry = FindArchiveEntry(virtualPath))
    {
        std::vector<uint8_t> bytes;
        if (!ReadFromArchive(*entry, bytes))
        {
            CCL_LOG_ERROR(LogCategory::Core, "[FileSystem] Failed to read from archive: %s", virtualPath.c_str());
            return {};
        }

        const std::chrono::duration<double, std::milli> elapsed =
            std::chrono::steady_clock::now() - t0;
        ReportSlowFileRead(virtualPath, elapsed.count(), bytes.size());
        return bytes;
    }

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