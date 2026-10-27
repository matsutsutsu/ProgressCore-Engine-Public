#pragma once
// ===========================================================================
// 概要: エンジンのファイルアクセスを1か所に集める仮想ファイルシステム（VFS）の入口。
//
// 責務:
//   - 仮想パスでのテキスト／バイナリ読み込みと、テキスト書き込み
//   - ファイルの存在確認と、フォルダ直下のファイル列挙
//   - 読み込みを Tracy のゾーンに記録し、遅い読み込みをメッセージとして残す
//
// 備考: Shipping 構成では起動時にアーカイブ（Data.pak）を開き、読み込み・存在確認・一覧は
//       「アーカイブ → ディスク」の順に探す。アーカイブに無いファイル（設定ファイルなど
//       プレイヤー側で書き換わるもの）は今まで通りディスクから読む。
//       Debug / Development はアーカイブを開かず、ディスクだけを見る。
//       アーカイブの作成は Tools/Pak作成ツール/make_pak.py（書式はそちらと揃えること）。
//       アーカイブの目次と中身は XOR で読めなくしてあり、読むときに戻す（書式は FileSystem.cpp）。
//
//       アーカイブの目次はパスを正規化して引く（\ と / を揃え、. と .. を解決し、英字を小文字にする）。
//       呼び出し側が "Assets\Models/..\x.json" のように書いても同じファイルに当たる。
//
// ===========================================================================

#include <string>
#include <vector>
#include <cstdint>
#include <filesystem>

class FileSystem
{
public:
    // 起動時に1度だけ呼ぶ（Shipping ではここでアーカイブを開く）
    static void Initialize();

    /**
     * @brief アーカイブ（.pak）を開き、目次を読み込む。以降の読み込みはアーカイブを先に探す。
     * @param archivePath アーカイブのパス（作業ディレクトリからの相対）
     * @return 開けて目次が正しく読めたら true。失敗したらアーカイブ無しのまま（ディスクだけを見る）
     * @note 他の読み込みより先に、メインスレッドで1度だけ呼ぶ。目次は以降書き換えない。
     */
    static bool Mount(const std::string &archivePath);

    // ファイルがアーカイブの中にあれば true（ディスクにあるだけなら false）
    static bool IsInArchive(const std::string &virtualPath);

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
     *       アーカイブを開いているときは、アーカイブの中とディスクの両方から集める（同じ名前は1つにまとめる）。
     */
    static std::vector<std::string> ListFiles(const std::string &virtualDir,
                                              const std::string &extension);

private:
    // 仮想パスを OS の物理パスに変換する
    static std::filesystem::path ResolvePhysicalPath(const std::string &virtualPath);
};
