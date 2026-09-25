#pragma once
// ===========================================================================
// 概要: エンジンのファイルアクセスを1か所に集める仮想ファイルシステム（VFS）の入口。
//
// 責務:
//   - 仮想パスでのテキスト／バイナリ読み込みと、テキスト書き込み
//   - ファイルの存在確認と、フォルダ直下のファイル列挙
//   - 読み込みを Tracy のゾーンに記録し、遅い読み込みをメッセージとして残す
//
// 備考: 現在は仮想パスをそのまま物理パスとして扱う。アセットはこのクラス経由で
//       読むことにしておき、将来アーカイブ（.pak）へ移しても呼び出し側を変えずに済むようにする。
//
// TODO(2026-05-24): Shipping 構成で .pak からの読み込みに対応する（ResolvePhysicalPath と
//   各読み込み関数をアーカイブの目次経由にし、書き込みはセーブデータ用フォルダへ振り分ける）
// ===========================================================================

#include <string>
#include <vector>
#include <cstdint>
#include <filesystem>

class FileSystem
{
public:
    // 起動時に1度だけ呼ぶ
    static void Initialize();

    /**
     * @brief テキストファイルを丸ごと読み込む。
     * @param virtualPath 仮想ルートからの相対パス（例: "Assets/Scene/Stage1.json"）
     * @return ファイル全体の文字列。開けなければ空文字列
     */
    static std::string ReadAllText(const std::string &virtualPath);

    /**
     * @brief テキストを書き込む（既存ファイルは上書き、フォルダが無ければ作る）。
     * @param virtualPath 書き込み先の仮想パス
     * @param content 書き込む文字列
     * @return 書き込みに成功したら true
     */
    static bool WriteAllText(const std::string &virtualPath, const std::string &content);

    /**
     * @brief バイナリファイルを丸ごと読み込む。
     * @param virtualPath 仮想ルートからの相対パス
     * @return 読み込んだバイト列。失敗時は空
     */
    static std::vector<uint8_t> ReadAllBytes(const std::string &virtualPath);

    // ファイルが存在すれば true
    static bool Exists(const std::string &virtualPath);

    /**
     * @brief フォルダ直下のファイルを列挙する（再帰しない）。
     * @param virtualDir 仮想ルートからの相対パス（例: "Assets/Formations"）
     * @param extension 絞り込む拡張子。"." を含めた小文字で指定（例: ".json"）。空なら全件
     * @return 見つかったファイルの仮想パス（名前順）。フォルダが無ければ空
     * @note 「フォルダに置けば増える」形のアセット向け。個々のパスをコードに書かずに
     *       済むので、陣形を1つ増やすのにリビルドが要らない。
     *       ディスクを走査するので、毎フレームではなく起動時・リロード時にだけ呼ぶ。
     */
    static std::vector<std::string> ListFiles(const std::string &virtualDir,
                                              const std::string &extension);

private:
    // 仮想パスを OS の物理パスに変換する
    static std::filesystem::path ResolvePhysicalPath(const std::string &virtualPath);
};
