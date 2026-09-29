#pragma once
// ===========================================================================
// 概要: Assets フォルダの中身を、フォルダツリーとアイコンのグリッドで表示するブラウザ。
//
// 責務:
//   - フォルダを走査して木構造を作る（Refresh で作り直す）
//   - 戻る・進む・上へ・パンくずリストでの移動
//   - 名前の検索と、種類ごとの表示フィルタ
//   - モデル・プレハブのファイルをドラッグ＆ドロップの送り元にする
//
// 備考: AssetBrowserWindow が所有して描く。アイコンは ShellIconLoader で OS 標準のものを使う。
// ===========================================================================
#include <string>
#include <vector>
#include <unordered_map>
#include <filesystem>
#include <d3d11.h>
#include <wrl/client.h>

namespace fs = std::filesystem;

class AssetBrowser
{
public:
    AssetBrowser(const std::string &rootPath, ID3D11Device *device);
    ~AssetBrowser();

    void Draw();
    void Refresh(); // フォルダを走査し直し、履歴と選択を初期化する

private:
    struct AssetNode
    {
        std::string            name;                // ファイル/フォルダ名
        fs::path               path;                // 実際のファイルパス
        bool                   isDirectory = false; // フォルダかどうか
        std::vector<AssetNode> children;            // 子要素
        AssetNode             *parent = nullptr;    // 親ノード（パンくずリスト用）
    };

    // ディレクトリ走査
    void LoadDirectory(const fs::path &dirPath, AssetNode &node);

    // parent を張り直す。children は vector なので、走査中の追加で要素が移動し、
    // その時点で取ったポインタは無効になる。木が完成してから一括で設定する
    void SetParents(AssetNode &node, AssetNode *parent);

    // ナビゲーション機能
    void NavigateTo(AssetNode *folder);
    void NavigateBack();
    void NavigateForward();
    void NavigateUp();

    // 描画系
    void DrawToolbar();
    void DrawBreadcrumbs();
    void DrawFolderTree(AssetNode &node);
    void DrawGridView();
    void DrawSearchBar();

    // ユーティリティ
    AssetNode *FindNodeByPath(AssetNode &node, const fs::path &path);
    bool       FilterAsset(const AssetNode &node); // 表示するなら true（フォルダは常に true）

private:
    AssetNode   rootNode; // ルートノード
    std::string rootPath; // ルートパス

    AssetNode  *currentFolder = nullptr; // 右ペインで表示中のフォルダ
    std::string selectedAssetPath;       // 選択中のアセットのパス

    std::vector<AssetNode *> historyBack;    // 戻る履歴
    std::vector<AssetNode *> historyForward; // 進む履歴

    std::unordered_map<std::string, std::string> extensionMap; // 拡張子→カテゴリ

    std::string                           searchQuery; // 検索クエリ
    std::unordered_map<std::string, bool> typeFilters; // 種別フィルタ

    ID3D11Device *device; // アイコンのテクスチャを作るデバイス（所有しない）
};