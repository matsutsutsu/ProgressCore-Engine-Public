#include "AssetBrowser.h"
#include <imgui.h>
#include <algorithm>
#include "ShellIconLoader.h"
#include "Engine/Platform/Core/Logger.h"
#include "Engine/Graphics/Resource/ModelResource.h" // .model.json の判定を共有する

#include <tracy/Tracy.hpp>

namespace
{
    /**
     * @brief 2つのペインの境目のつまみを描く。ドラッグした分だけ size0 と size1 をやり取りする。
     * @param split_vertically true なら左右に分ける縦のつまみ、false なら上下に分ける横のつまみ。
     * @param thickness        つまみの太さ（px）。
     * @param size0            1つ目（左・上）のペインの大きさ（px）。ドラッグで書き換える。
     * @param size1            2つ目（右・下）のペインの大きさ（px）。ドラッグで書き換える。
     * @param min_size0        1つ目のペインの最小の大きさ（px）。
     * @param min_size1        2つ目のペインの最小の大きさ（px）。
     */
    void Splitter(bool split_vertically, float thickness, float *size0, float *size1, float min_size0, float min_size1)
    {
        ImVec2 backup_pos = ImGui::GetCursorPos();
        ImGui::Button("##splitter", split_vertically ? ImVec2(thickness, *size0 + *size1) : ImVec2(*size0 + *size1, thickness));
        if (ImGui::IsItemActive())
        {
            float delta = split_vertically ? ImGui::GetIO().MouseDelta.x : ImGui::GetIO().MouseDelta.y;
            if (delta < min_size0 - *size0) delta = min_size0 - *size0;
            if (delta > *size1 - min_size1) delta = *size1 - min_size1;
            *size0 += delta;
            *size1 -= delta;
        }
        ImGui::SetCursorPos(backup_pos);
    }

    /**
     * @brief ファイルをドラッグ＆ドロップの送り元にする。受け取るのは HierarchyWindow。
     *
     * ツリーとグリッドの両方から呼ぶ（別々に書くと、受け付ける拡張子が食い違う）。
     *
     * @param filePath 送るファイルのパス。
     * @param name     ドラッグ中に表示する名前。
     */
    void DragSourceForFile(const std::string &filePath, const std::string &name)
    {
        if (!ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) return;

        // .model.json も拡張子は .json なので、プレハブより先に判定する
        if (ModelResource::IsModelSetFile(filePath.c_str()))
        {
            ImGui::SetDragDropPayload("ASSET_MODEL", filePath.c_str(), filePath.size() + 1);
            ImGui::Text("Load Model: %s", name.c_str());
        }
        else if (filePath.find(".json") != std::string::npos)
        {
            ImGui::SetDragDropPayload("ASSET_PREFAB", filePath.c_str(), filePath.size() + 1);
            ImGui::Text("Spawn Prefab: %s", name.c_str());
        }
        else if (filePath.find(".glb") != std::string::npos || filePath.find(".gltf") != std::string::npos ||
                 filePath.find(".fbx") != std::string::npos || filePath.find(".obj") != std::string::npos)
        {
            ImGui::SetDragDropPayload("ASSET_MODEL", filePath.c_str(), filePath.size() + 1);
            ImGui::Text("Load Model: %s", name.c_str());
        }
        ImGui::EndDragDropSource();
    }
} // namespace

// ============================================================
// コンストラクタ / デストラクタ
// ============================================================
AssetBrowser::AssetBrowser(const std::string &rootPath, ID3D11Device *device)
    : rootPath(rootPath), device(device)
{
    ZoneScopedN("AssetBrowser::Constructor");

    extensionMap = {
        {".fbx", "model"},
        {".obj", "model"},
        {".glb", "model"},
        {".gltf", "model"},
        {".png", "texture"},
        {".jpg", "texture"},
        {".jpeg", "texture"},
        {".cpp", "script"},
        {".h", "script"},
        {".json", "prefab"},
    };

    typeFilters["model"]   = true;
    typeFilters["texture"] = true;
    typeFilters["script"]  = true;
    typeFilters["prefab"]  = true;

    Refresh();
}

AssetBrowser::~AssetBrowser()
{
    ShellIconLoader::ClearCache();
}

// ============================================================
// ツリー構築・初期化
// ============================================================
void AssetBrowser::Refresh()
{
    ZoneScopedN("AssetBrowser::Refresh");

    rootNode.children.clear();
    rootNode.path        = rootPath;
    rootNode.name        = "Assets";
    rootNode.isDirectory = true;
    rootNode.parent      = nullptr;

    LoadDirectory(rootPath, rootNode);

    SetParents(rootNode, nullptr);

    historyBack.clear();
    historyForward.clear();
    currentFolder     = &rootNode;
    selectedAssetPath = "";

    CCL_LOG_INFO(LogCategory::Editor, "[AssetBrowser] Refreshed directory tree.");
}

void AssetBrowser::SetParents(AssetNode &node, AssetNode *parent)
{
    node.parent = parent;
    for (auto &child : node.children)
    {
        SetParents(child, &node);
    }
}

void AssetBrowser::LoadDirectory(const fs::path &dirPath, AssetNode &node)
{
    ZoneScopedN("AssetBrowser::LoadDirectory");

    if (!fs::exists(dirPath) || !fs::is_directory(dirPath)) return;

    for (auto &entry : fs::directory_iterator(dirPath))
    {
        if (!fs::exists(entry.path())) continue;

        AssetNode child;
        child.name        = entry.path().filename().u8string();
        child.path        = entry.path();
        child.isDirectory = fs::is_directory(entry.path());

        if (child.isDirectory)
        {
            LoadDirectory(entry.path(), child);
        }
        else
        {
            // 空のファイル（サイズ 0）と、サイズを取れないファイルは表示しない
            std::error_code ec;
            if (fs::file_size(entry.path(), ec) && !ec)
            {
                node.children.push_back(std::move(child));
            }
            continue;
        }
        node.children.push_back(std::move(child));
    }

    std::sort(node.children.begin(), node.children.end(), [](const AssetNode &a, const AssetNode &b) {
        if (a.isDirectory != b.isDirectory) return a.isDirectory > b.isDirectory;
        return a.name < b.name;
    });
}

AssetBrowser::AssetNode *AssetBrowser::FindNodeByPath(AssetNode &node, const fs::path &path)
{
    if (node.path == path) return &node;
    for (auto &child : node.children)
    {
        if (auto found = FindNodeByPath(child, path)) return found;
    }
    return nullptr;
}

// ============================================================
// ナビゲーション（履歴）
// ============================================================
void AssetBrowser::NavigateTo(AssetNode *folder)
{
    if (currentFolder && currentFolder != folder)
    {
        historyBack.push_back(currentFolder);
        historyForward.clear();
    }
    currentFolder     = folder;
    selectedAssetPath = "";
}

void AssetBrowser::NavigateBack()
{
    if (!historyBack.empty())
    {
        historyForward.push_back(currentFolder);
        currentFolder = historyBack.back();
        historyBack.pop_back();
        selectedAssetPath = "";
    }
}

void AssetBrowser::NavigateForward()
{
    if (!historyForward.empty())
    {
        historyBack.push_back(currentFolder);
        currentFolder = historyForward.back();
        historyForward.pop_back();
        selectedAssetPath = "";
    }
}

void AssetBrowser::NavigateUp()
{
    if (currentFolder && currentFolder->parent)
    {
        NavigateTo(currentFolder->parent);
    }
}

// ============================================================
// 描画
// ============================================================
void AssetBrowser::Draw()
{
    ZoneScopedN("AssetBrowser::Draw");

    ImGui::Begin("Asset Browser");

    DrawToolbar();
    ImGui::Separator();

    // 左右ペインの幅。全インスタンスで共有する（ブラウザは1つしか作らない）
    static float leftWidth  = 200.0f;
    static float rightWidth = 400.0f;
    float        totalWidth = ImGui::GetContentRegionAvail().x;

    if (leftWidth + rightWidth < totalWidth)
        rightWidth = totalWidth - leftWidth;

    // 左ペイン (フォルダツリー)
    ImGui::BeginChild("LeftPane", ImVec2(leftWidth, 0), true);
    DrawFolderTree(rootNode);
    ImGui::EndChild();

    ImGui::SameLine();
    Splitter(true, 4.0f, &leftWidth, &rightWidth, 100.0f, 100.0f);
    ImGui::SameLine();

    // 右ペイン (コンテンツグリッド)
    ImGui::BeginChild("RightPane", ImVec2(rightWidth, 0), true);
    DrawGridView();
    ImGui::EndChild();

    ImGui::End();
}

// ============================================================
// ツールバーとパンくずリスト
// ============================================================
void AssetBrowser::DrawToolbar()
{
    ZoneScopedN("AssetBrowser::DrawToolbar");

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 4));

    if (ImGui::Button("Refresh"))
    {
        Refresh();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();

    ImGui::BeginDisabled(historyBack.empty());
    if (ImGui::Button("<")) NavigateBack();
    ImGui::EndDisabled();
    ImGui::SameLine();

    ImGui::BeginDisabled(historyForward.empty());
    if (ImGui::Button(">")) NavigateForward();
    ImGui::EndDisabled();
    ImGui::SameLine();

    ImGui::BeginDisabled(currentFolder == nullptr || currentFolder->parent == nullptr);
    if (ImGui::Button("^")) NavigateUp();
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();

    DrawBreadcrumbs();
    ImGui::PopStyleVar();

    float searchWidth = 200.0f;
    ImGui::SameLine(ImGui::GetWindowWidth() - searchWidth - 15.0f);
    DrawSearchBar();
}

void AssetBrowser::DrawBreadcrumbs()
{
    if (!currentFolder) return;

    std::vector<AssetNode *> pathNodes;
    AssetNode               *curr = currentFolder;
    while (curr != nullptr)
    {
        pathNodes.push_back(curr);
        curr = curr->parent;
    }
    std::reverse(pathNodes.begin(), pathNodes.end());

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    for (size_t i = 0; i < pathNodes.size(); ++i)
    {
        if (i > 0)
        {
            ImGui::SameLine();
            ImGui::TextDisabled(">");
            ImGui::SameLine();
        }
        if (ImGui::Button(pathNodes[i]->name.c_str()))
        {
            NavigateTo(pathNodes[i]);
        }
    }
    ImGui::PopStyleColor();
}

void AssetBrowser::DrawSearchBar()
{
    ImGui::SetNextItemWidth(150.0f);
    char buffer[256];
    strncpy_s(buffer, sizeof(buffer), searchQuery.c_str(), _TRUNCATE);
    if (ImGui::InputTextWithHint("##Search", "Search...", buffer, sizeof(buffer)))
    {
        searchQuery = buffer;
    }

    if (ImGui::IsItemHovered())
    {
        ImGui::BeginTooltip();
        ImGui::Text("Filter by type:");
        ImGui::Separator();
        ImGui::Checkbox(".fbx/.obj/.glb/.gltf", &typeFilters["model"]);
        ImGui::Checkbox(".png/.jpg", &typeFilters["texture"]);
        ImGui::Checkbox(".cpp/.h", &typeFilters["script"]);
        ImGui::Checkbox(".json", &typeFilters["prefab"]);
        ImGui::EndTooltip();
    }
}

// ============================================================
// 左ペイン（フォルダツリー。1回のクリックで展開し、ファイルも表示する）
// ============================================================
void AssetBrowser::DrawFolderTree(AssetNode &node)
{
    ZoneScopedN("AssetBrowser::DrawFolderTree");

    // 検索や拡張子フィルタに引っかかったファイルはツリーからも隠す
    if (!FilterAsset(node)) return;

    std::string id = node.path.string();
    ImGui::PushID(id.c_str());

    // 行全体をクリックの対象にする
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth;

    if (node.isDirectory)
    {
        if (currentFolder == &node) flags |= ImGuiTreeNodeFlags_Selected;

        // OpenOnArrow / OpenOnDoubleClick は付けない。1回のクリックで展開・収縮させるため
    }
    else
    {
        if (selectedAssetPath == node.path.string()) flags |= ImGuiTreeNodeFlags_Selected;

        // ファイルは矢印なし（Leaf）にし、TreePush もしない
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    }

    if (node.parent == nullptr && node.isDirectory)
    {
        flags |= ImGuiTreeNodeFlags_DefaultOpen;
    }

    // OSネイティブアイコンの取得（ファイル/フォルダ共通）
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> iconSrv = ShellIconLoader::GetIconSRV(node.path.wstring(), device);

    // ツリーノードの描画（ラベルは空にし、後からアイコンと名前を並べる）
    bool nodeOpen = ImGui::TreeNodeEx("##TreeItem", flags, "");

    // クリックで右ペインの表示を切り替える
    if (ImGui::IsItemClicked())
    {
        if (node.isDirectory)
        {
            NavigateTo(&node);
        }
        else
        {
            // ファイルなら、右ペインに親フォルダを出してそのファイルを選択する
            if (node.parent)
            {
                NavigateTo(node.parent);
            }
            selectedAssetPath = node.path.string();
        }
    }

    // アイコンと名前
    ImGui::SameLine();
    if (iconSrv)
    {
        ImGui::Image((ImTextureID)iconSrv.Get(), ImVec2(16, 16));
    }
    else
    {
        ImGui::Dummy(ImVec2(16, 16)); // アイコン取得失敗時の余白
    }

    ImGui::SameLine();
    ImGui::Text("%s", node.name.c_str());

    // ツリーからもドラッグできるようにする
    if (!node.isDirectory)
    {
        DragSourceForFile(node.path.string(), node.name);
    }

    // 開いているフォルダだけ子を描く
    if (node.isDirectory)
    {
        if (nodeOpen)
        {
            for (auto &child : node.children)
            {
                DrawFolderTree(child);
            }
            ImGui::TreePop(); // NoTreePushOnOpen が付いていないので閉じる必要がある
        }
    }

    ImGui::PopID();
}

// ============================================================
// 右ペイン（アイコンのグリッド）
// ============================================================
void AssetBrowser::DrawGridView()
{
    // 重い場合は ImGuiListClipper で見えている行だけを描く
    ZoneScopedN("AssetBrowser::DrawGridView");

    AssetNode   *folder   = currentFolder ? currentFolder : &rootNode;
    static float iconSize = 72.0f;

    float availX = ImGui::GetContentRegionAvail().x;
    ImGui::SetCursorPosX(availX - 120.0f);
    ImGui::SetNextItemWidth(100.0f);
    ImGui::SliderFloat("##IconSize", &iconSize, 48.0f, 128.0f, "Size: %.0f");
    ImGui::Separator();

    float padding     = 16.0f;
    float cellSize    = iconSize + padding;
    int   columnCount = (std::max)(1, (int)(ImGui::GetContentRegionAvail().x / cellSize));

    if (ImGui::BeginTable("AssetGrid", columnCount))
    {
        for (auto &child : folder->children)
        {
            if (!FilterAsset(child)) continue;

            ImGui::TableNextColumn();
            ImGui::PushID(child.path.string().c_str());

            bool isSelected = (selectedAssetPath == child.path.string());

            ImVec4 bgColor = isSelected ? ImVec4(0.3f, 0.6f, 1.0f, 0.5f) : ImVec4(0, 0, 0, 0);
            ImGui::PushStyleColor(ImGuiCol_Button, bgColor);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.4f, 0.7f, 1.0f, 0.6f));

            // アイコンは自前で用意せず、エクスプローラと同じ OS 標準アイコンを使う
            Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> iconSrv;
            iconSrv = ShellIconLoader::GetIconSRV(child.path.wstring(), device);

            if (iconSrv)
            {
                ImGui::ImageButton("##icon", (ImTextureID)iconSrv.Get(), ImVec2(iconSize, iconSize));
            }
            else
            {
                ImGui::Button("?", ImVec2(iconSize, iconSize));
            }

            if (ImGui::IsItemHovered())
            {
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                {
                    if (child.isDirectory)
                    {
                        NavigateTo(&child);
                    }
                }
                else if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                {
                    selectedAssetPath = child.path.string();
                }
            }
            ImGui::PopStyleColor(2);

            if (!child.isDirectory)
            {
                DragSourceForFile(child.path.string(), child.name);
            }

            ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + iconSize);
            ImGui::Text("%s", child.name.c_str());
            ImGui::PopTextWrapPos();

            ImGui::PopID();
        }
        ImGui::EndTable();
    }
}

// ============================================================
// フィルタ判定
// ============================================================
bool AssetBrowser::FilterAsset(const AssetNode &node)
{
    // ループ内で数千回呼ばれるので ZoneScopedN を入れない（計測の負荷で操作できないほど重くなる）。
    // 拡張子が extensionMap に無いファイルは表示しない。

    if (node.isDirectory) return true;

    if (!searchQuery.empty())
    {
        std::string nameLower  = node.name;
        std::string queryLower = searchQuery;
        std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), ::tolower);
        std::transform(queryLower.begin(), queryLower.end(), queryLower.begin(), ::tolower);

        if (nameLower.find(queryLower) == std::string::npos)
        {
            return false;
        }
    }

    // .model.json は拡張子が .json だが中身はモデルなので、モデル側に分類する
    if (ModelResource::IsModelSetFile(node.path.string().c_str()))
    {
        return typeFilters.count("model") == 0 || typeFilters["model"];
    }

    auto ext = node.path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    auto it = extensionMap.find(ext);
    if (it != extensionMap.end())
    {
        const std::string &type = it->second;
        if (typeFilters.count(type) && !typeFilters[type])
        {
            return false;
        }
    }
    else
    {
        return false;
    }

    return true;
}