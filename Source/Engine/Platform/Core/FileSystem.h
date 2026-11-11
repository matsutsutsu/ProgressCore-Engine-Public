#pragma once
// ===========================================================================
// 概要: エンジンのファイルアクセスを1か所に集める仮想ファイルシステム（VFS）の入口
//
// 責務:
//   - 仮想パスでのテキスト／バイナリ読み込みと、テキスト書き込み
//   - ファイルの存在確認と、フォルダ直下のファイル列挙
//   - 読み込みを Tracy のゾーンに記録し、遅い読み込みをメッセージとして残す
//
// 備考: Shipping は起動時にアーカイブ（Data.pak）を開き、アーカイブ、ディスクの順に探す。Debug / Development はディスクだけ
//       アーカイブは Tools/Pak作成ツール/make_pak.py で作る。目次と中身は XOR で隠してある
//       目次はパスを正規化（区切り・. と ..・大小文字）して引くので、書き方が違っても同じファイルに当たる
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

    // アーカイブ（.pak）を開いて目次を読む。以降はアーカイブを先に探す。失敗したらディスクだけを見る
    // ほかの読み込みより先に、メインスレッドで1回だけ呼ぶ
    static bool Mount(const std::string &archivePath);

    // ファイルがアーカイブの中にあれば true（ディスクにあるだけなら false）
    static bool IsInArchive(const std::string &virtualPath);

    // テキストファイルを丸ごと読む（例: "Assets/Scene/Stage1.json"）。開けなければ空
    static std::string ReadAllText(const std::string &virtualPath);

    // テキストを書く。既存のファイルは上書きし、フォルダがなければ作る
    static bool WriteAllText(const std::string &virtualPath, const std::string &content);

    // バイナリファイルを丸ごと読む。失敗したら空
    static std::vector<uint8_t> ReadAllBytes(const std::string &virtualPath);

    // ファイルが存在すれば true
    static bool Exists(const std::string &virtualPath);

    // フォルダの直下のファイルを、名前の順に列挙する（再帰しない）。extension は "." 付きの小文字で、空なら全部
    // 置けば増える形のアセット（陣形など）に使う。ディスクを走査するので、起動時と読み直しのときだけ呼ぶ
    // アーカイブを開いているときは、アーカイブとディスクの両方から集める（同じ名前は1つ）
    static std::vector<std::string> ListFiles(const std::string &virtualDir,
                                              const std::string &extension);

private:
    // 仮想パスを OS の物理パスに変換する
    static std::filesystem::path ResolvePhysicalPath(const std::string &virtualPath);
};
