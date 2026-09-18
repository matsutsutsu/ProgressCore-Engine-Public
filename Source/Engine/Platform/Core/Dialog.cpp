//==============================================================================
// Dialog.cpp
//==============================================================================
#include "Dialog.h"

#include <commdlg.h>
#include <fstream>
#include <iomanip>
#include <unordered_map>
#include <json.hpp>
#include <filesystem> // std::filesystem を使用

//==========================================================================
// 内部ユーティリティ (匿名名前空間)
//==========================================================================
namespace
{
    //--- 文字コード変換 ---------------------------------------------------

    // UTF-8 -> UTF-16
    std::wstring Utf8ToWide(std::string_view utf8)
    {
        if (utf8.empty()) return {};
        int len = ::MultiByteToWideChar(CP_UTF8, 0,
                                        utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
        if (len <= 0) return {};
        std::wstring out(static_cast<size_t>(len), L'\0');
        ::MultiByteToWideChar(CP_UTF8, 0,
                              utf8.data(), static_cast<int>(utf8.size()), out.data(), len);
        return out;
    }

    // UTF-16 -> UTF-8
    std::string WideToUtf8(std::wstring_view w)
    {
        if (w.empty()) return {};
        int len = ::WideCharToMultiByte(CP_UTF8, 0,
                                        w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
        if (len <= 0) return {};
        std::string out(static_cast<size_t>(len), '\0');
        ::WideCharToMultiByte(CP_UTF8, 0,
                              w.data(), static_cast<int>(w.size()), out.data(), len, nullptr, nullptr);
        return out;
    }

    //--- グローバル状態 ---------------------------------------------------

    // 履歴: contextId -> 最後に開いたディレクトリ
    std::unordered_map<std::string, std::filesystem::path> s_history;

    // プリセット: contextId -> 設定
    std::unordered_map<std::string, FileDialogPreset> s_presets;

    //--- フィルタ文字列構築 ----------------------------------------------

    /**
     * Win32 OFN の lpstrFilter 形式に変換する
     * 形式: "Label\0Pattern\0Label2\0Pattern2\0\0"
     */
    std::wstring BuildFilterStringW(const std::vector<FileFilter> &filters)
    {
        std::wstring result;
        auto         append = [&](std::wstring_view s) {
            result.append(s);
            result.push_back(L'\0');
        };

        if (filters.empty())
        {
            append(L"All Files");
            append(L"*.*");
        }
        else
        {
            for (const auto &f : filters)
            {
                append(Utf8ToWide(f.label));
                append(Utf8ToWide(f.pattern));
            }
        }
        result.push_back(L'\0'); // 終端
        return result;
    }

    //--- ヘルパー --------------------------------------------------------

    // 履歴キー解決 : contextId > title > "Default"
    std::string ResolveContextId(const FileDialogParams &params)
    {
        if (!params.contextId.empty()) return params.contextId;
        if (!params.title.empty()) return params.title;
        return "Default";
    }

    /**
     * 初期ディレクトリ解決 (優先度順)
     *   1. params.initialPath が指定 (ファイル指定ならその親、ディレクトリならそのまま)
     *   2. 履歴に contextId がある
     *   3. プリセットに defaultDir がある
     *   4. 空 (Win32 が現在のドキュメントフォルダ等を使う)
     */
    std::filesystem::path ResolveInitialDir(
        const FileDialogParams &params, const std::string &contextId)
    {
        // 1. 明示パス最優先
        if (!params.initialPath.empty())
        {
            if (params.initialPath.has_filename() && params.initialPath.has_extension())
            {
                return params.initialPath.parent_path();
            }
            return params.initialPath;
        }

        // 2. 履歴
        if (auto it = s_history.find(contextId); it != s_history.end())
        {
            return it->second;
        }

        // 3. プリセット
        if (auto it = s_presets.find(contextId); it != s_presets.end())
        {
            if (!it->second.defaultDir.empty())
            {
                return it->second.defaultDir;
            }
        }
        return {};
    }

    // 履歴に保存する (選択されたパスからディレクトリ部分のみ)
    void StoreHistory(const std::string &contextId, const std::filesystem::path &selected)
    {
        if (selected.empty()) return;
        const auto dir       = selected.has_filename() ? selected.parent_path() : selected;
        s_history[contextId] = dir;
    }

    // パス区切りを Windows 用 (バックスラッシュ) に正規化
    std::wstring NormalizeSeparatorW(std::wstring s)
    {
        for (auto &c : s)
            if (c == L'/') c = L'\\';
        return s;
    }

    // --- パス変換ユーティリティ ---
    // 絶対パスをカレントパス基準の相対パスに変換
    std::filesystem::path MakeRelative(const std::filesystem::path &p)
    {
        if (p.is_relative()) return p;
        try
        {
            return std::filesystem::relative(p, std::filesystem::current_path());
        }
        catch (...)
        {
            return p;
        }
    }

    // 相対パスを絶対パスへ復元
    std::filesystem::path MakeAbsolute(const std::filesystem::path &p)
    {
        if (p.is_absolute()) return p;
        return std::filesystem::absolute(p);
    }

    //--- 共通ダイアログ起動 (Open) ---------------------------------------

    struct OpenInvokeResult
    {
        bool                               ok = false;
        std::vector<std::filesystem::path> paths;
    };

    OpenInvokeResult InvokeOpenW(const FileDialogParams &params, bool multiSelect)
    {
        const std::string contextId  = ResolveContextId(params);
        const auto        initialDir = ResolveInitialDir(params, contextId);

        const std::wstring wTitle      = Utf8ToWide(params.title);
        const std::wstring wFilter     = BuildFilterStringW(params.filters);
        const std::wstring wInitialDir = NormalizeSeparatorW(initialDir.wstring());

        // マルチ選択時のフォーマット ("Dir\0F1\0F2\0\0") に備え十分大きく確保
        constexpr DWORD      kBufElements = 32 * 1024;
        std::vector<wchar_t> fileBuf(kBufElements, L'\0');

        // initialPath にファイル名まで含まれていればそれを初期値に
        if (!params.initialPath.empty() && params.initialPath.has_filename())
        {
            const auto   wname = params.initialPath.filename().wstring();
            const size_t n     = (std::min)(wname.size(), static_cast<size_t>(kBufElements - 1));
            std::copy_n(wname.begin(), n, fileBuf.begin());
        }

        OPENFILENAMEW ofn{};
        ofn.lStructSize = sizeof(ofn);
        // オーナー窓が未指定なら、現在アクティブなアプリ窓を親にする。
        //   オーナー無しのモーダルダイアログはゲーム窓の裏に隠れ、単一スレッドの
        //   ゲームループを止めたまま前面に出ず「フリーズ」に見える事故を防ぐ。
        ofn.hwndOwner       = params.ownerHwnd ? params.ownerHwnd : ::GetActiveWindow();
        ofn.lpstrFilter     = wFilter.c_str();
        ofn.nFilterIndex    = 1;
        ofn.lpstrFile       = fileBuf.data();
        ofn.nMaxFile        = kBufElements;
        ofn.lpstrTitle      = wTitle.empty() ? nullptr : wTitle.c_str();
        ofn.lpstrInitialDir = wInitialDir.empty() ? nullptr : wInitialDir.c_str();
        ofn.Flags           = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY | OFN_NOCHANGEDIR;
        if (multiSelect)
        {
            ofn.Flags |= OFN_ALLOWMULTISELECT | OFN_EXPLORER;
        }

        // OFN_NOCHANGEDIR を指定しているが、念のためカレントディレクトリを保護
        wchar_t     curDir[MAX_PATH] = {};
        const DWORD cdLen            = ::GetCurrentDirectoryW(MAX_PATH, curDir);

        const BOOL ret = ::GetOpenFileNameW(&ofn);

        if (cdLen > 0 && cdLen < MAX_PATH)
        {
            ::SetCurrentDirectoryW(curDir);
        }

        OpenInvokeResult result;
        if (!ret) return result;

        if (multiSelect)
        {
            // "Dir\0File1\0File2\0\0" 形式 / または単一フルパス
            const std::filesystem::path dir(fileBuf.data());
            wchar_t                    *p = fileBuf.data() + std::wcslen(fileBuf.data()) + 1;
            if (*p == L'\0')
            {
                // 単一選択された
                result.paths.push_back(dir);
            }
            else
            {
                while (*p)
                {
                    result.paths.push_back(dir / std::filesystem::path(p));
                    p += std::wcslen(p) + 1;
                }
            }
        }
        else
        {
            result.paths.emplace_back(fileBuf.data());
        }

        if (!result.paths.empty())
        {
            StoreHistory(contextId, result.paths.front());
        }
        result.ok = true;
        return result;
    }

} // namespace

//==========================================================================
// FileDialog::Initialize
//==========================================================================

void FileDialog::Initialize(const std::filesystem::path &settingsPath)
{
    // ----------------------------------------------------------------
    // エンジン共通プリセットを一括登録
    // ※ defaultDir はプロジェクト構成に合わせて変更する
    // ----------------------------------------------------------------
    RegisterPreset("Assets", {
                                 .filters    = {{"Assets Files", "*.json"}, {"All Files", "*.*"}},
                                 .defaultDir = "Assets",
                                 .defaultExt = "json",
                             });

    RegisterPreset("Scene", {
                                .filters    = {{"Scene Files", "*.json"}, {"All Files", "*.*"}},
                                .defaultDir = "Assets/Scene",
                                .defaultExt = "json",
                            });
    RegisterPreset("Model", {
                                // *.model.json は本体モデルと追加アニメーションを束ねた定義ファイル
                                .filters    = {{"Model Files", "*.fbx;*.obj;*.gltf;*.glb;*.model.json"}, {"All Files", "*.*"}},
                                .defaultDir = "Assets/Models",
                            });
    RegisterPreset("Texture", {
                                  .filters    = {{"Image Files", "*.png;*.jpg;*.tga;*.bmp;*.dds"}, {"All Files", "*.*"}},
                                  .defaultDir = "Assets/Textures",
                              });
    RegisterPreset("TextEditor", {
                                     .filters = {
                                         {"Supported Files", "*.json;*.lua;*.cpp;*.h;*.hlsl;*.hlsli"},
                                         {"HLSL Shaders", "*.hlsl;*.hlsli"},
                                         {"JSON Files", "*.json"},
                                         {"Lua Scripts", "*.lua"},
                                         {"All Files", "*.*"},
                                     },
                                     .defaultDir = "Assets",
                                 });
    RegisterPreset("AnimSequence", {
                                       .filters    = {{"JSON Files", "*.json"}, {"All Files", "*.*"}},
                                       .defaultDir = "Assets/Animations/Sequencer",
                                       .defaultExt = "json",
                                   });
    RegisterPreset("AnimNode", {
                                   .filters    = {{"JSON Files", "*.json"}, {"All Files", "*.*"}},
                                   .defaultDir = "Assets/Animations/Nodes",
                                   .defaultExt = "json",
                               });
    RegisterPreset("Prefab", {
                                 .filters    = {{"JSON Files", "*.json"}, {"All Files", "*.*"}},
                                 .defaultDir = "Assets/Prefabs",
                                 .defaultExt = "json",
                             });
    RegisterPreset("Audio", {
                                .filters    = {{"Audio Files", "*.wav;*.ogg;*.mp3;*.flac"}, {"All Files", "*.*"}},
                                .defaultDir = "Assets/Audio",
                            });
    RegisterPreset("BehaviorTreeNode", {
                                           .filters    = {{"Behavior Tree", "*.json"}},
                                           .defaultDir = "Assets/BehaviorTree",
                                           .defaultExt = "json",
                                       });

    // ----------------------------------------------------------------
    // 前回の履歴を読み込む
    // ----------------------------------------------------------------
    LoadHistory(settingsPath);
}

//==========================================================================
// FileDialog public API
//==========================================================================

std::optional<std::filesystem::path> FileDialog::Open(const FileDialogParams &params)
{
    auto r = InvokeOpenW(params, /*multiSelect=*/false);
    if (!r.ok || r.paths.empty()) return std::nullopt;
    return r.paths.front();
}

std::vector<std::filesystem::path> FileDialog::OpenMulti(const FileDialogParams &params)
{
    return InvokeOpenW(params, /*multiSelect=*/true).paths;
}

std::optional<std::filesystem::path> FileDialog::Save(const FileDialogParams &params)
{
    const std::string contextId  = ResolveContextId(params);
    const auto        initialDir = ResolveInitialDir(params, contextId);

    const std::wstring wTitle      = Utf8ToWide(params.title);
    const std::wstring wFilter     = BuildFilterStringW(params.filters);
    const std::wstring wInitialDir = NormalizeSeparatorW(initialDir.wstring());

    // デフォルト拡張子 (paramsで指定が無ければプリセットから補完)
    std::wstring wExt = Utf8ToWide(params.defaultExt);
    if (wExt.empty())
    {
        if (auto it = s_presets.find(contextId); it != s_presets.end())
        {
            wExt = Utf8ToWide(it->second.defaultExt);
        }
    }

    constexpr DWORD      kBufElements = 32 * 1024;
    std::vector<wchar_t> fileBuf(kBufElements, L'\0');

    // デフォルトファイル名を埋める
    std::filesystem::path defaultName = params.defaultFileName;
    if (defaultName.empty() && !params.initialPath.empty() && params.initialPath.has_filename())
    {
        defaultName = params.initialPath.filename();
    }
    if (!defaultName.empty())
    {
        const auto   wname = defaultName.wstring();
        const size_t n     = (std::min)(wname.size(), static_cast<size_t>(kBufElements - 1));
        std::copy_n(wname.begin(), n, fileBuf.begin());
    }

    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    // オーナー窓が未指定なら現在アクティブなアプリ窓を親にする（Open 側と同じ理由）。
    ofn.hwndOwner       = params.ownerHwnd ? params.ownerHwnd : ::GetActiveWindow();
    ofn.lpstrFilter     = wFilter.c_str();
    ofn.nFilterIndex    = 1;
    ofn.lpstrFile       = fileBuf.data();
    ofn.nMaxFile        = kBufElements;
    ofn.lpstrTitle      = wTitle.empty() ? nullptr : wTitle.c_str();
    ofn.lpstrInitialDir = wInitialDir.empty() ? nullptr : wInitialDir.c_str();
    ofn.lpstrDefExt     = wExt.empty() ? nullptr : wExt.c_str();
    ofn.Flags           = OFN_OVERWRITEPROMPT | OFN_HIDEREADONLY | OFN_NOCHANGEDIR;

    wchar_t     curDir[MAX_PATH] = {};
    const DWORD cdLen            = ::GetCurrentDirectoryW(MAX_PATH, curDir);

    const BOOL ret = ::GetSaveFileNameW(&ofn);

    if (cdLen > 0 && cdLen < MAX_PATH)
    {
        ::SetCurrentDirectoryW(curDir);
    }
    if (!ret) return std::nullopt;

    std::filesystem::path selected(fileBuf.data());
    StoreHistory(contextId, selected);
    return selected;
}

std::string FileDialog::ConvertToRelativePath(const std::filesystem::path &absolutePath)
{
    if (absolutePath.empty()) return "";

    std::error_code ec;
    // 現在の作業ディレクトリ（通常はプロジェクトのルート、または実行ファイルのある場所）を取得
    std::filesystem::path currentPath = std::filesystem::current_path(ec);

    if (ec)
    {
        // カレントディレクトリの取得に失敗した場合は、UTF-8文字列としてそのまま返す
        return WideToUtf8(absolutePath.wstring());
    }

    // absolutePath を currentPath を基準とした相対パスに変換する
    std::filesystem::path relativePath = std::filesystem::proximate(absolutePath, currentPath, ec);

    if (ec)
    {
        // 別のドライブ（C: と D: など）に跨っている等の理由で変換に失敗した場合
        return WideToUtf8(absolutePath.wstring());
    }

    // Windows特有のバックスラッシュ '\\' を、Unreal/Unity等でも一般的なスラッシュ '/' に正規化する
    std::string relativeStr = WideToUtf8(relativePath.wstring());
    std::replace(relativeStr.begin(), relativeStr.end(), '\\', '/');

    return relativeStr;
}

//--- プリセット ---------------------------------------------------------

void FileDialog::RegisterPreset(std::string_view contextId, const FileDialogPreset &preset)
{
    s_presets[std::string(contextId)] = preset;
}

std::optional<std::filesystem::path> FileDialog::OpenPreset(
    std::string_view contextId, std::string_view title, HWND ownerHwnd)
{
    FileDialogParams p;
    p.contextId = std::string(contextId);
    p.title     = title.empty() ? p.contextId : std::string(title);
    p.ownerHwnd = ownerHwnd;
    if (auto it = s_presets.find(p.contextId); it != s_presets.end())
    {
        p.filters = it->second.filters;
    }
    return Open(p);
}

std::optional<std::filesystem::path> FileDialog::SavePreset(
    std::string_view contextId, std::string_view title,
    const std::filesystem::path &defaultFileName, HWND ownerHwnd)
{
    FileDialogParams p;
    p.contextId       = std::string(contextId);
    p.title           = title.empty() ? p.contextId : std::string(title);
    p.defaultFileName = defaultFileName;
    p.ownerHwnd       = ownerHwnd;
    if (auto it = s_presets.find(p.contextId); it != s_presets.end())
    {
        p.filters    = it->second.filters;
        p.defaultExt = it->second.defaultExt;
    }
    return Save(p);
}

//--- 履歴 ---------------------------------------------------------------

void FileDialog::LoadHistory(const std::filesystem::path &settingsPath)
{
    std::ifstream file(settingsPath);
    if (!file.is_open()) return;
    try
    {
        nlohmann::json j;
        file >> j;
        if (j.contains("DialogHistory"))
        {
            for (auto &item : j["DialogHistory"].items())
            {
                std::string utf8 = item.value().get<std::string>();
                // 保存されている相対パスを絶対パスに戻す
                s_history[item.key()] = MakeAbsolute(std::filesystem::path(Utf8ToWide(utf8)));
            }
        }
    }
    catch (...)
    {
        // 破損時はサイレントに無視 (デフォルト起動)
    }
}

void FileDialog::SaveHistory(const std::filesystem::path &settingsPath)
{
    nlohmann::json j;

    // 他のエディタ設定を消さないよう既存内容を読み込む
    {
        std::ifstream inFile(settingsPath);
        if (inFile.is_open())
        {
            try
            {
                inFile >> j;
            }
            catch (...)
            {
            }
        }
    }

    // 履歴部分を上書き
    j["DialogHistory"] = nlohmann::json::object();
    for (const auto &[key, path] : s_history)
    {
        // 絶対パスを相対パスに変換してから保存
        j["DialogHistory"][key] = WideToUtf8(MakeRelative(path).wstring());
    }

    // 出力先ディレクトリを保証
    const auto parent = std::filesystem::path(settingsPath).parent_path();
    if (!parent.empty())
    {
        std::error_code ec;
        std::filesystem::create_directories(parent, ec);
    }

    std::ofstream outFile(settingsPath);
    if (outFile.is_open())
    {
        outFile << std::setw(4) << j << std::endl;
    }
}

void FileDialog::ClearHistory(std::string_view contextId)
{
    if (contextId.empty())
        s_history.clear();
    else
        s_history.erase(std::string(contextId));
}
