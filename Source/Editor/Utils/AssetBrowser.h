#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <filesystem>
#include <d3d11.h>
#include <DirectXTex.h>
#include <wrl/client.h>

namespace fs = std::filesystem;

class AssetBrowser
{
  public:
    AssetBrowser(const std::string &rootPath, ID3D11Device *device);
    ~AssetBrowser();

    void Draw();
    void Refresh();

  private:
    struct AssetNode
    {
        std::string            name;                // ファイル/フォルダ名
        fs::path               path;                // 実際のファイルパス
        bool                   isDirectory = false; // フォルダかどうか
        std::vector<AssetNode> children;            // 子要素
        AssetNode             *parent = nullptr;    // ★追加: 親ノードへのポインタ（パンくずリスト用）
    };


    // ディレクトリ走査
    void LoadDirectory(const fs::path &dirPath, AssetNode &node);
    void SetParents(AssetNode &node, AssetNode *parent); // ★追加: ポインタの安全な再構築

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
    AssetNode  *FindNodeByPath(AssetNode &node, const fs::path &path);
    bool        FilterAsset(const AssetNode &node);

  private:
    AssetNode   rootNode; // ルートノード
    std::string rootPath; // ルートパス

    // ★ 状態管理アーキテクチャの刷新
    AssetNode  *currentFolder = nullptr; // 右ペインで表示中のフォルダ
    std::string selectedAssetPath;       // 選択中のアセットのパス

    std::vector<AssetNode *> historyBack;    // 戻る履歴
    std::vector<AssetNode *> historyForward; // 進む履歴

    std::unordered_map<std::string, std::string> extensionMap; // 拡張子→カテゴリ

    std::string                           searchQuery; // 検索クエリ
    std::unordered_map<std::string, bool> typeFilters; // 種別フィルタ

    ID3D11Device *device; // D3D11デバイス（アイコン作成用）
};