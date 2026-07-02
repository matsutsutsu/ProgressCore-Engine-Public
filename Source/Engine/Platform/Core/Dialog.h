#pragma once
//==============================================================================
// Dialog.h
//
// ファイル選択 / 保存ダイアログ
//
// 設計方針:
//   - 用途別ID (contextId) で履歴を独立管理
//   - 内部は UTF-16 (Win32 W API)、外向きは UTF-8 (std::string / path) で統一
//   - 構造体パラメータ + std::optional 戻り値のモダンC++設計
//   - プリセット登録による呼び出しの簡潔化
//
//==============================================================================

#include <Windows.h>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>


/**
 * @brief ダイアログの結果コード
 */
enum class DialogResult
{
    OK,
    Cancel,
    Yes,
    No,
};

/**
 * @brief ファイルフィルタ1個分
 * @details 例) label="Model Files", pattern="*.fbx;*.obj;*.gltf"
 */
struct FileFilter
{
    std::string label;     ///< 表示名
    std::string pattern;   ///< セミコロン区切りのワイルドカード
};

/**
 * @brief FileDialog::Open / Save に渡すパラメータ
 *
 * @details
 *  C++20 の指定初期化子で必要項目だけ書けるように、すべて初期値を持つ。
 *  履歴のキーは contextId が最優先、無ければ title。
 */
struct FileDialogParams
{
    std::string title;                       ///< ダイアログタイトル (UTF-8)
    std::string contextId;                   ///< 履歴キー ("Model","Scene"等). 空ならtitle使用
    std::vector<FileFilter> filters;         ///< ファイルフィルタ. 空ならAll Files
    std::filesystem::path initialPath;       ///< 明示初期パス. 設定時は履歴より優先
    std::filesystem::path defaultFileName;   ///< Save時のデフォルトファイル名
    std::string defaultExt;                  ///< Save時のデフォルト拡張子 (例: "scene")
    HWND ownerHwnd = nullptr;                ///< オーナーウィンドウ
};

/**
 * @brief 用途プリセット - 用途別の典型設定を事前登録するための構造体
 */
struct FileDialogPreset
{
    std::vector<FileFilter> filters;
    std::filesystem::path defaultDir;        ///< 履歴が無い時のデフォルト初期位置
    std::string defaultExt;
};

/**
 * @brief ファイル選択 / 保存ダイアログ (販売エンジン向け)
 *
 * @details
 *  - 用途別(contextId)に独立した履歴を持ち、用途間で初期位置が混線しない
 *  - 明示 initialPath があれば履歴より優先
 *  - 内部は UTF-16 / std::filesystem::path で扱い、多言語パスに対応
 *  - 履歴は LoadHistory / SaveHistory で JSON 永続化可能
 */
class FileDialog
{
public:
    //------------------------------------------------------------------
    // 初期化
    //------------------------------------------------------------------

    /**
     * @brief エンジン全体で使うプリセットを一括登録し、履歴を読み込む
     *
     * @param settingsPath EditorPrefs.json のパス
     * @details
     *  Framework のコンストラクタから 1 度だけ呼び出す。
     *  この関数でプリセット登録 + LoadHistory をまとめて行うため、
     *  各エディタウィンドウ側でフィルタ文字列を書く必要がなくなる。
     *
     *  【登録済みプリセット一覧】
     *  | contextId     | フィルタ                                | 用途                  |
     *  |---------------|-----------------------------------------|-----------------------|
     *  | "Scene"       | *.json                                  | シーン Open / Save    |
     *  | "Model"       | *.fbx;*.obj;*.gltf                      | モデル選択            |
     *  | "Texture"     | *.png;*.jpg;*.tga;*.bmp;*.dds           | テクスチャ選択        |
     *  | "TextEditor"  | *.json;*.lua;*.cpp;*.h;*.hlsl;*.hlsli  | テキストエディタ      |
     *  | "AnimSequence"| *.json                                  | アニメーションシーケンス |
     *  | "Prefab"      | *.json                                  | プレハブ Save         |
     *  | "Audio"       | *.wav;*.ogg;*.mp3;*.flac                | オーディオアセット    |
     */
    static void Initialize(const std::filesystem::path& settingsPath = "Build/EditorPrefs.json");

    //------------------------------------------------------------------
    // 基本API
    //------------------------------------------------------------------

    /**
     * @brief 単一ファイルを開くダイアログを表示する
     * @param params ダイアログパラメータ
     * @return 選択されたパス. キャンセル時は std::nullopt
     */
    static std::optional<std::filesystem::path> Open(const FileDialogParams& params);

    /**
     * @brief 複数ファイルを開くダイアログを表示する
     * @param params ダイアログパラメータ
     * @return 選択されたパス群. キャンセル時は空ベクター
     */
    static std::vector<std::filesystem::path> OpenMulti(const FileDialogParams& params);

    /**
     * @brief 保存ダイアログを表示する
     * @param params ダイアログパラメータ
     * @return 保存パス. キャンセル時は std::nullopt
     */
    static std::optional<std::filesystem::path> Save(const FileDialogParams& params);

    /**
     * @brief 絶対パスをプロジェクトのルート（または実行ファイル位置）からの相対パスに変換する
     * @param absolutePath ダイアログ等で取得した絶対パス
     * @return 変換後の相対パス（文字列）。変換に失敗した場合（別ドライブ等）はそのまま返す。
     */
    static std::string ConvertToRelativePath(const std::filesystem::path& absolutePath);

    //------------------------------------------------------------------
    // プリセット - 呼び出し側を簡潔にする
    //------------------------------------------------------------------

    /**
     * @brief 用途プリセットを登録する
     * @param contextId 用途キー
     * @param preset 設定内容
     * @details 一度登録すれば OpenPreset / SavePreset でワンライナー呼び出し可能
     */
    static void RegisterPreset(std::string_view contextId, const FileDialogPreset& preset);

    /**
     * @brief 登録済みプリセットでファイルを開く
     * @param contextId 用途キー (RegisterPreset で登録したもの)
     * @param title 表示タイトル. 空なら contextId が使われる
     * @param ownerHwnd オーナーウィンドウ
     */
    static std::optional<std::filesystem::path> OpenPreset(
        std::string_view contextId,
        std::string_view title = {},
        HWND ownerHwnd = nullptr);

    /**
     * @brief 登録済みプリセットで保存ダイアログを表示する
     */
    static std::optional<std::filesystem::path> SavePreset(
        std::string_view contextId,
        std::string_view title = {},
        const std::filesystem::path& defaultFileName = {},
        HWND ownerHwnd = nullptr);

    //------------------------------------------------------------------
    // 履歴管理
    //------------------------------------------------------------------

    /**
     * @brief 履歴をJSONファイルから読み込む
     * @param settingsPath 設定ファイルのパス
     */
    static void LoadHistory(const std::filesystem::path& settingsPath = "Build/User/EditorPrefs.json");

    /**
     * @brief 履歴をJSONファイルに書き込む
     * @param settingsPath 設定ファイルのパス
     */
    static void SaveHistory(const std::filesystem::path& settingsPath = "Build/User/EditorPrefs.json");

    /**
     * @brief 履歴をクリアする
     * @param contextId 対象キー. 空ならすべてクリア
     */
    static void ClearHistory(std::string_view contextId = {});
};


