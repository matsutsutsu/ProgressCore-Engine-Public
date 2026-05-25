#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <filesystem>

/**
 * @brief ゲーム内のすべてのファイルアクセスを一元管理する仮想ファイルシステム (VFS)
 * @details
 * 将来の製品版（Shipping）において、.pak などのアーカイブファイルからの
 * メモリロードや暗号化の復号を、ゲームロジックに意識させることなく透過的に行います。
 * エンジン内のすべてのアセット（JSON, テクスチャ等）はこのクラスを経由して読み込んでください。
 */
class FileSystem {
public:
    /**
     * @brief VFSの初期化処理を行います。
     * @details アプリケーション起動時に1度だけ呼び出してください。
     * 将来的に .pak ファイルのマウントなどはここで実行されます。
     */
    static void Initialize();

    /**
     * @brief 指定されたパスのテキストファイルを読み込み、文字列として返します。
     * @param virtualPath 仮想ルートからの相対パス (例: "Assets/Scene/Stage1.json")
     * @return 読み込んだファイル全体の文字列。ファイルが存在しない・読み込み失敗時は空文字列を返します。
     */
    static std::string ReadAllText(const std::string& virtualPath);

    /**
     * @brief 指定されたパスにテキストデータを書き込みます。
     * @param virtualPath 書き込み先の仮想パス
     * @param content 書き込む文字列データ
     * @return 書き込みに成功した場合は true、失敗した場合は false
     * @warning Shippingビルドではアセットへの直接書き込みが禁止されるため、将来的にこの関数はセーブデータ専用領域などにルーティングされます。
     */
    static bool WriteAllText(const std::string& virtualPath, const std::string& content);

    /**
     * @brief 指定されたパスのバイナリファイルを読み込み、バイト配列として返します。
     * @param virtualPath 仮想ルートからの相対パス
     * @return 読み込んだバイト配列 (std::vector<uint8_t>)。失敗時は空の配列を返します。
     */
    static std::vector<uint8_t> ReadAllBytes(const std::string& virtualPath);

    /**
     * @brief 指定されたファイルが存在するかどうかを判定します。
     * @param virtualPath 仮想ルートからの相対パス
     * @return 存在する場合は true、それ以外は false
     */
    static bool Exists(const std::string& virtualPath);

private:
    /**
     * @brief 仮想パスをOSの物理パスに変換します。
     * @param virtualPath 仮想ルートからの相対パス
     * @return OSが認識できる物理パス
     */
    static std::filesystem::path ResolvePhysicalPath(const std::string& virtualPath);
};