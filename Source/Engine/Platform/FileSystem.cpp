#include "FileSystem.h"
#include "Engine/Platform/Logger.h" // ログ機能 (CCL_LOG等) に合わせて変更してください
#include <fstream>
#include <sstream>

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

std::string FileSystem::ReadAllText(const std::string& virtualPath)
{
    // TODO: Shipping構成時は .pak から展開したメモリブロックを std::string にして返す処理に分岐

    auto physicalPath = ResolvePhysicalPath(virtualPath);

    std::ifstream file(physicalPath, std::ios::in | std::ios::binary);
    if (!file.is_open()) {
        CCL_LOG_ERROR(LogCategory::Core, "[FileSystem] Failed to open text file: %s", virtualPath.c_str());
        return "";
    }

    // ストリームバッファ全体を一気に文字列として読み込む高速な手法
    std::ostringstream ss;
    ss << file.rdbuf();
    return ss.str();
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
    return true;
}

std::vector<uint8_t> FileSystem::ReadAllBytes(const std::string& virtualPath)
{
    // TODO: Shipping構成時は .pak から解凍したバイト配列を返す処理に分岐

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
        return buffer;
    }

    return {};
}