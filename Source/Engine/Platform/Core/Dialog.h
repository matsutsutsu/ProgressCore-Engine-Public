#pragma once
// ===========================================================================
// 概要: Win32 のファイル選択／保存ダイアログを、用途別の履歴とプリセット付きで包むクラス。
//
// 責務:
//   - 用途ID（contextId）ごとに最後に開いたフォルダを覚え、次回の初期位置にする
//   - 用途別のフィルタ・初期フォルダ・既定拡張子をプリセットとして登録する
//   - 履歴を EditorPrefs.json へ保存／読み込みする
//   - 絶対パスをカレントディレクトリ基準の相対パスへ変換する
//
// 備考: 内部は UTF-16（Win32 W API）、外向きは UTF-8（std::string / path）で扱う。
//       エディタ専用で、Framework が ENABLE_EDITOR のときだけ Initialize する。
// ===========================================================================

#include <Windows.h>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// ファイルフィルタ1個分。例: label="Model Files", pattern="*.fbx;*.obj;*.gltf"
struct FileFilter
{
    std::string label;   // 表示名
    std::string pattern; // セミコロン区切りのワイルドカード
};

/**
 * @brief FileDialog::Open / Save に渡すパラメータ。
 *
 * C++20 の指定初期化子で必要な項目だけ書けるよう、すべて初期値を持つ。
 * 履歴のキーは contextId を優先し、空なら title を使う。
 */
struct FileDialogParams
{
    std::string             title;               // ダイアログタイトル（UTF-8）
    std::string             contextId;           // 履歴キー（"Model", "Scene" 等）。空なら title を使う
    std::vector<FileFilter> filters;             // ファイルフィルタ。空なら All Files
    std::filesystem::path   initialPath;         // 明示の初期パス。指定時は履歴より優先する
    std::filesystem::path   defaultFileName;     // Save 時の既定ファイル名
    std::string             defaultExt;          // Save 時の既定拡張子（例: "json"）
    HWND                    ownerHwnd = nullptr; // オーナーウィンドウ。nullptr ならアクティブウィンドウ
};

// 用途プリセット。用途ごとの典型設定を事前登録しておく
struct FileDialogPreset
{
    std::vector<FileFilter> filters;    // ファイルフィルタ
    std::filesystem::path   defaultDir; // 履歴が無いときの初期フォルダ
    std::string             defaultExt; // Save 時の既定拡張子
};

class FileDialog
{
public:
    // ============================================================
    // 初期化
    // ============================================================

    /**
     * @brief エンジン共通のプリセットを登録し、履歴を読み込む。
     *
     * 登録するプリセット（contextId）: Assets / Scene / Model / Texture / TextEditor /
     * AnimSequence / AnimNode / Prefab / Audio / BehaviorTreeNode。
     * フィルタと初期フォルダは Dialog.cpp の Initialize を参照。
     *
     * @param settingsPath 履歴を読む EditorPrefs.json のパス
     * @note Framework の初期化から1度だけ呼ぶ。
     */
    static void Initialize(const std::filesystem::path &settingsPath = "Build/User/EditorPrefs.json");

    // ============================================================
    // 基本 API
    // ============================================================

    // 単一ファイルを開くダイアログを表示する。キャンセル時は std::nullopt
    static std::optional<std::filesystem::path> Open(const FileDialogParams &params);

    // 複数ファイルを選べる、開くダイアログを表示する。キャンセル時は空
    static std::vector<std::filesystem::path> OpenMulti(const FileDialogParams &params);

    // 保存ダイアログを表示する。キャンセル時は std::nullopt
    static std::optional<std::filesystem::path> Save(const FileDialogParams &params);

    /**
     * @brief 絶対パスをカレントディレクトリ基準の相対パスに変換する。
     * @param absolutePath ダイアログ等で取得した絶対パス
     * @return 区切りを '/' にした相対パス。別ドライブ等で変換できなければ元のパスをそのまま返す。
     */
    static std::string ConvertToRelativePath(const std::filesystem::path &absolutePath);

    // ============================================================
    // プリセット
    // ============================================================

    // 用途プリセットを登録する。以後 OpenPreset / SavePreset で contextId だけ渡せば使える
    static void RegisterPreset(std::string_view contextId, const FileDialogPreset &preset);

    /**
     * @brief 登録済みプリセットでファイルを開く。
     * @param contextId 用途キー（RegisterPreset で登録したもの）
     * @param title 表示タイトル。空なら contextId を使う
     * @param ownerHwnd オーナーウィンドウ
     */
    static std::optional<std::filesystem::path> OpenPreset(
        std::string_view contextId,
        std::string_view title     = {},
        HWND             ownerHwnd = nullptr);

    // 登録済みプリセットで保存ダイアログを表示する。引数は OpenPreset と同じ
    static std::optional<std::filesystem::path> SavePreset(
        std::string_view             contextId,
        std::string_view             title           = {},
        const std::filesystem::path &defaultFileName = {},
        HWND                         ownerHwnd       = nullptr);

    // ============================================================
    // 履歴
    // ============================================================

    // 履歴を JSON ファイルから読み込む。保存時の相対パスは絶対パスへ戻す
    static void LoadHistory(const std::filesystem::path &settingsPath = "Build/User/EditorPrefs.json");

    // 履歴を JSON ファイルに書き込む。同じファイルの他のキーは残す
    static void SaveHistory(const std::filesystem::path &settingsPath = "Build/User/EditorPrefs.json");

    // contextId の履歴を消す。空なら全部消す
    static void ClearHistory(std::string_view contextId = {});
};
