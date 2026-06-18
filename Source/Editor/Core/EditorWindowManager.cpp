#include "EditorWindowManager.h"

#include "Engine/Serialization/Serializer/SceneSerializer.h"
#include "Engine/Graphics/RHI/Graphics.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Platform/Core/Dialog.h"

#include "Editor/Core/EditorCommandHistory.h"

#include "Engine/Transform/Components/TransformComponent.h"

// 各ウィンドウのヘッダをインクルード
#include "Editor/Windows/AssetBrowserWindow.h"
#include "Editor/Windows/ECSDebugWindow.h"
#include "Editor/Windows/GameViewWindow.h"
#include "Editor/Windows/HierarchyWindow.h"
#include "Editor/Windows/InspectorWindow.h"
#include "Editor/Windows/AnimationSequencerWindow.h"
#include "Editor/Windows/TextEditorWindow.h"
#include "Editor/Windows/InputEditorWindow.h"
#include "Editor/Windows/ConsoleWindow.h"
#include "Editor/Windows/NavMeshWindow.h"
#include "Editor/Windows/AnimGraphWindow.h"
#include "Editor/Windows/BehaviorTreeWindow.h"
#include "Editor/Windows/MemoryAnalyzerWindow.h"

#include "Editor/Windows/DungeonGeneratorWindow.h"

#include "Engine/Serialization/Factory/Prefab.h"
#include "Engine/Platform/Core/FileSystem.h" // 既存のJSONライブラリ利用
#include <fstream>
#include <json.hpp> // nlohmann/json を利用
#include <imgui.h>
#include <filesystem>
#include <windows.h>
#include <shellapi.h> // ShellExecuteA を使用するために必要
#include <map>
#include <sstream>

namespace fs = std::filesystem;

void EditorWindowManager::Initialize()
{
    // =========================================================
    // ★ 常に表示しておきたいメインウィンドウ (デフォルトで true)
    // =========================================================
    RegisterWindow<InspectorWindow>();
    RegisterWindow<HierarchyWindow>();
    RegisterWindow<AssetBrowserWindow>();

    // =========================================================
    // ★ デフォルトでは「非表示」にするサブウィンドウ
    // (->SetVisible(false) をつけるだけで、起動時は隠れます)
    // =========================================================
    RegisterWindow<ECSDebugWindow>();
    RegisterWindow<ConsoleWindow>();
    //RegisterWindow<AnimationSequencerWindow>();
    //RegisterWindow<AnimGraphWindow>();
	//RegisterWindow<BehaviorTreeWindow>();
    RegisterWindow<DungeonGeneratorWindow>();

    // ※まだ実装中で完全に読み込みたくない（クラッシュする等の）場合は、
    // 引き続きコメントアウトで対応してください。
    RegisterWindow<TextEditorWindow>();
    RegisterWindow<InputEditorWindow>();
    RegisterWindow<NavMeshWindow>();
    RegisterWindow<MemoryAnalyzerWindow>();
    //RegisterWindow<SceneViewWindow>();
    RegisterWindow<GameViewWindow>();

}


// =========================================================
// ★ プレハブのフォルダ階層をメニュー化するためのヘルパー
// =========================================================
namespace {
    struct PrefabNode {
        bool isFile = false;
        std::string fullPath;
        std::map<std::string, std::unique_ptr<PrefabNode>> children;
    };

    // 再帰的にメニューを描画する関数
    void DrawPrefabNodeTree(const std::string& nodeName, const PrefabNode& node, EditorContext& context)
    {
        if (node.isFile) {
            // ファイルの場合：拡張子(.json)を消して見た目を綺麗にする
            std::string displayName = std::filesystem::path(nodeName).stem().string();
            std::string menuText = "  " + displayName; // 少しインデントしてアイコンっぽく

            if (ImGui::MenuItem(menuText.c_str())) {
                if (context.world) {
                    DirectX::XMFLOAT3 spawnPos = { 0, 5, 0 };
                    Prefab::SpawnPrefab(*context.world, node.fullPath, spawnPos);
                    CCL_LOG_INFO(LogCategory::Editor, "Spawned Prefab: %s", displayName.c_str());
                }
            }
        }
        else {
            // フォルダの場合：サブメニューを作成
            std::string menuText = "[+] " + nodeName;
            if (ImGui::BeginMenu(menuText.c_str())) {
                // UX向上: フォルダを先に、ファイルを後に並べる
                for (const auto& [childName, childNode] : node.children) {
                    if (!childNode->isFile) DrawPrefabNodeTree(childName, *childNode, context);
                }
                for (const auto& [childName, childNode] : node.children) {
                    if (childNode->isFile) DrawPrefabNodeTree(childName, *childNode, context);
                }
                ImGui::EndMenu();
            }
        }
    }
}


// プレハブフォルダをスキャンしてリストを更新する関数
void RefreshPrefabList(EditorContext &context)
{
    context.prefabFiles.clear();

    // スキャン対象のディレクトリ (必要に応じて変更してください)
    std::string targetPath = "Assets/Prefabs";

    if (fs::exists(targetPath)) {
        try {
            // 再帰的にディレクトリを探索
            for (const auto &entry : fs::recursive_directory_iterator(targetPath)) {
                // .json ファイルのみをリストに追加
                if (entry.is_regular_file() && entry.path().extension() == ".json") {
                    // パスを汎用形式 (スラッシュ区切り) に変換して保存
                    context.prefabFiles.push_back(entry.path().generic_string());
                }
            }
        }
        catch (const fs::filesystem_error &e) {
            OutputDebugStringA(e.what());
        }
    }
}


// Draw関数の実装
void EditorWindowManager::Draw(EditorContext &context)
{
    ZoneScopedN("EditorWindowManager::Draw");

    // EditorScene::OnDrawImGui もしくは Update の中で
    if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z)) {
        EditorCommandHistory::Instance().Undo();
    }
    if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y)) {
        EditorCommandHistory::Instance().Redo();
    }

    // 1. ショートカットキー (F1) で表示切替
    // ImGuiがアクティブでなくても効くようにします
    if (ImGui::IsKeyPressed(ImGuiKey_F1, false)) {
        _isVisible = !_isVisible;
    }

    // 初回のみリストを自動生成
    if (context.prefabFiles.empty()) {
        RefreshPrefabList(context);
    }

    // =========================================================
    //  F1非表示時は、背景キャンバスにゲーム画面をフルスクリーン描画する
    // =========================================================
    if (!_isVisible) {
        if (context.gameSRV) {
            // ImGuiの描画可能領域全体（画面サイズ）を取得
            ImVec2 displaySize = ImGui::GetIO().DisplaySize;

            // UIの後ろの壁紙としてゲーム画面を直接焼き付ける
            ImGui::GetBackgroundDrawList()->AddImage(
                context.gameSRV,
                ImVec2(0, 0),         // 左上
                displaySize           // 右下
            );
        }

        // ヒントUI
        //ImGui::SetNextWindowPos(ImVec2(10, 10));
        //ImGui::SetNextWindowBgAlpha(0.3f);
        //ImGui::Begin("HiddenOverlay", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav);
        //ImGui::TextColored(ImVec4(1, 1, 0, 1), "[F1] Show Editor UI");
        //ImGui::End();

        return; // ウィンドウ群やメニューバーは描画せずに終了
    }

    // 3. 表示状態なら描画実行
    DrawMainMenuBar(context);

    for (auto &window : _windows) {
        window->Render(context);
    }
}



void EditorWindowManager::DrawMainMenuBar(EditorContext &context)
{
    // この関数は全体を統括する「目次」に徹する
    if (ImGui::BeginMainMenuBar()) {
        DrawFileMenu(context);
        DrawEntityMenu(context);
        DrawPrefabMenu(context);
        DrawSceneMenu(context);
        DrawWindowMenu(context);

        DrawToolsMenu(context);

        // 中央寄せのプレイボタン
        DrawPlayControlBar(context);

        // 最後に右端へ飛ばしてFPSを描画
        DrawPerformanceStatsBar(context);

        ImGui::EndMainMenuBar();
    }
}


void EditorWindowManager::DrawFileMenu(EditorContext& context)
{
    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("Open Scene...", "Ctrl+O")) {

            HWND hWnd = Graphics::Instance().GetWindowHandle();
          
            if (auto path = Engine::FileDialog::OpenPreset("Scene", "Open Scene", hWnd)) {
                context.pendingLoadScenePath = path->string();
                context.selectedEntity = CCL::ECS::InvalidEntityID;
            }
        }
        if (ImGui::MenuItem("Save Scene", "Ctrl+S")) {

            HWND hWnd = Graphics::Instance().GetWindowHandle();
            if (auto path = Engine::FileDialog::SavePreset("Scene", "Save Scene", {}
                ,hWnd)) {
                context.pendingSaveScenePath = path->string();
                context.selectedEntity = CCL::ECS::InvalidEntityID;
            }
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Exit")) {
            PostQuitMessage(0);
        }
        ImGui::EndMenu();
    }
}

void EditorWindowManager::DrawEntityMenu(EditorContext& context)
{
    if (ImGui::BeginMenu("Entity")) {
        if (ImGui::MenuItem("Add Empty Entity")) {
            Archetype arch = ArchetypeHelper::Generate<TransformComponent>();
            context.world->RequestSpawnEntity(arch);
        }
        ImGui::EndMenu();
    }
}

void EditorWindowManager::DrawPrefabMenu(EditorContext& context)
{
    if (ImGui::BeginMenu("Prefab")) {
        if (ImGui::MenuItem("Refresh List")) {
            RefreshPrefabList(context);
        }
        ImGui::Separator();

        if (context.prefabFiles.empty()) {
            ImGui::TextDisabled("No prefabs found");
        }
        else {
            // =========================================================
            // 1. パスを解析してツリー構造（フォルダ階層）を構築する
            // =========================================================
            PrefabNode rootNode;
            std::string basePrefix = "Assets/Prefabs";
            std::replace(basePrefix.begin(), basePrefix.end(), '\\', '/');

            for (const auto& pathStr : context.prefabFiles) {
                std::string relativePath = pathStr;
                std::replace(relativePath.begin(), relativePath.end(), '\\', '/');

                // "Assets/Prefabs/" の部分を取り除く
                size_t pos = relativePath.find(basePrefix);
                if (pos == 0) {
                    relativePath = relativePath.substr(basePrefix.length());
                    // 先頭の '/' を取り除く
                    if (!relativePath.empty() && relativePath[0] == '/') {
                        relativePath = relativePath.substr(1);
                    }
                }

                // スラッシュで分割してノードを作成
                std::stringstream ss(relativePath);
                std::string token;
                PrefabNode* current = &rootNode;

                std::vector<std::string> parts;
                while (std::getline(ss, token, '/')) {
                    parts.push_back(token);
                }

                for (size_t i = 0; i < parts.size(); ++i) {
                    const auto& part = parts[i];
                    if (current->children.find(part) == current->children.end()) {
                        current->children[part] = std::make_unique<PrefabNode>();
                    }
                    current = current->children[part].get();

                    if (i == parts.size() - 1) { // 最後の要素（ファイル本体）
                        current->isFile = true;
                        current->fullPath = pathStr;
                    }
                }
            }

            // =========================================================
            // 2. ツリーを描画する（フォルダを上、ファイルを下に表示）
            // =========================================================
            for (const auto& [childName, childNode] : rootNode.children) {
                if (!childNode->isFile) DrawPrefabNodeTree(childName, *childNode, context);
            }
            for (const auto& [childName, childNode] : rootNode.children) {
                if (childNode->isFile) DrawPrefabNodeTree(childName, *childNode, context);
            }
        }
        ImGui::EndMenu();
    }
}

void EditorWindowManager::DrawSceneMenu(EditorContext& context)
{
    if (ImGui::BeginMenu("Scene")) {
        //if (ImGui::MenuItem("Title Scene")) SceneManager::Instance().ChangeScene("Title");
        //if (ImGui::MenuItem("Game Scene")) SceneManager::Instance().ChangeScene("Game");
        if (ImGui::MenuItem("Particle Scene")) SceneManager::Instance().ChangeScene("Particle");
        if (ImGui::MenuItem("Editor Scene")) SceneManager::Instance().ChangeScene("Editor");
        //if (ImGui::MenuItem("Shop Scene")) SceneManager::Instance().ChangeScene("Shop");
        ImGui::EndMenu();
    }
}

void EditorWindowManager::DrawWindowMenu(EditorContext& context)
{
    if (ImGui::BeginMenu("Window")) {
        if (ImGui::MenuItem("Show UI", "F1", &_isVisible)) {
            // 変数に紐付いているのでクリックで自動トグルされる
        }
        ImGui::Separator();
        for (auto& window : _windows) {
            bool open = window->GetVisible();
            if (ImGui::MenuItem(window->GetName().c_str(), nullptr, &open)) {
                window->SetVisible(open);
            }
        }
        ImGui::EndMenu();
    }
}

void EditorWindowManager::DrawPlayControlBar(EditorContext& context)
{
    float menuBarWidth = ImGui::GetWindowWidth();
    float buttonWidth = 50.0f;
    float totalButtonsWidth = buttonWidth * 2.0f + ImGui::GetStyle().ItemSpacing.x;

    // 中央寄せ
    ImGui::SameLine(menuBarWidth * 0.5f - totalButtonsWidth * 0.5f);

    ImVec2 btnSize = ImVec2(buttonWidth, 0);

    // Stop
    if (context.playState != EditorPlayState::Stopped) {
        if (ImGui::Button("Stop", btnSize)) {
            context.playState = EditorPlayState::Stopped;
            context.pendingLoadScenePath = context.tempBackupScenePath;
            CCL_LOG_INFO(LogCategory::Editor, "Play In Editor: Stopped and Restored.");
        }
    }
    else {
        ImGui::BeginDisabled();
        ImGui::Button("Stop", btnSize);
        ImGui::EndDisabled();
    }

    ImGui::SameLine();

    // Play / Pause / Resume
    if (context.playState == EditorPlayState::Stopped) {
        if (ImGui::Button("Play", btnSize)) {
            context.pendingSaveScenePath = context.tempBackupScenePath;
            context.playState = EditorPlayState::Playing;
            CCL_LOG_INFO(LogCategory::Editor, "Play In Editor: Started.");
        }
    }
    else if (context.playState == EditorPlayState::Playing) {
        if (ImGui::Button("Pause", btnSize)) {
            context.playState = EditorPlayState::Paused;
        }
    }
    else if (context.playState == EditorPlayState::Paused) {
        if (ImGui::Button("Resume", btnSize)) {
            context.playState = EditorPlayState::Playing;
        }
    }
}

void EditorWindowManager::DrawPerformanceStatsBar(EditorContext& context)
{
    ImGuiIO& io = ImGui::GetIO();
    float fps = io.Framerate;
    // 0除算防止を入れつつ、ミリ秒(ms)を計算
    float ms = 1000.0f / (fps > 0.0f ? fps : 1.0f);

    // 表示するテキストをフォーマット
    char textBuffer[128];
    snprintf(textBuffer, sizeof(textBuffer), "%.1f FPS (%.2f ms)", fps, ms);

    // テキストの描画幅を計算し、右端からのオフセットを決定
    float textWidth = ImGui::CalcTextSize(textBuffer).x;
    float rightMargin = 20.0f; // 右端の余白

    // 現在のカーソル位置から、ウィンドウ右端へ一気にジャンプ
    ImGui::SameLine(ImGui::GetWindowWidth() - textWidth - rightMargin);

    // 直感的に状況がわかるよう、60FPSを基準に信号色（緑→黄→赤）で色分け
    ImVec4 color;
    if (fps >= 55.0f)      color = ImVec4(0.4f, 1.0f, 0.4f, 1.0f); // 快適（緑）
    else if (fps >= 30.0f) color = ImVec4(1.0f, 1.0f, 0.4f, 1.0f); // 警告（黄）
    else                   color = ImVec4(1.0f, 0.4f, 0.4f, 1.0f); // 危険（赤）

    ImGui::TextColored(color, "%s", textBuffer);

    // =========================================================
    // ★ おすすめの追加UX: マウスホバーで詳細なプロファイルを表示
    // =========================================================
    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::Text("Performance & ECS Status");
        ImGui::Separator();

        // ImGui::PlotLines に渡すための自作リングバッファ（履歴配列）
        static float frameTimes[120] = { 0 };
        static int frameTimeIdx = 0;

        // 現在のフレームタイム(ms)を配列に記録し、インデックスを進める
        frameTimes[frameTimeIdx] = ms;
        frameTimeIdx = (frameTimeIdx + 1) % 120;

        // グラフの描画
        // 引数: ラベル, 配列, 要素数, オフセット位置, テキスト, Y軸最小, Y軸最大, グラフのサイズ
        // ※Y軸の最大値を 33.3f (30FPS相当のms) に固定することで、重い処理（スパイク）が飛び抜けて視覚化されます。
        ImGui::PlotLines("Frame Time(ms)", frameTimes, 120, frameTimeIdx, "ms", 0.0f, 33.3f, ImVec2(200, 50));

        if (context.world) {
            ImGui::Spacing();

            // 現在のエンティティ数やチャンク数をサッと確認できるようにする
            size_t totalEntities = 0;
            size_t activeChunks = 0;
            for (const auto& chunk : context.world->GetChunkManager().GetChunks()) {
                if (chunk) {
                    totalEntities += chunk->GetEntityCount();
                    activeChunks++;
                }
            }
            ImGui::TextDisabled("Active Entities:"); ImGui::SameLine(); ImGui::Text("%zu", totalEntities);
            ImGui::TextDisabled("Active Chunks:");   ImGui::SameLine(); ImGui::Text("%zu", activeChunks);
        }
        ImGui::EndTooltip();
    }
}

// =========================================================
//  Tools メニューの実装
// =========================================================
void EditorWindowManager::DrawToolsMenu(EditorContext& context)
{
    if (ImGui::BeginMenu("Tools")) {

        if (ImGui::MenuItem("Launch Tracy Profiler")) {
            // Windows APIを使用して外部のexeを非同期起動する
            // カレントディレクトリ（通常はslnのある場所）からの相対パスを指定
            HINSTANCE result = ShellExecuteA(
                nullptr,
                "open",
                "Tools\\Tracy\\tracy-profiler.exe",
                nullptr, // コマンドライン引数
                nullptr, // ワーキングディレクトリ (nullptrで現在のディレクトリを引き継ぐ)
                SW_SHOW  // 通常のウィンドウとして表示
            );

            // 戻り値が32以下の場合はエラー発生
            if ((INT_PTR)result <= 32) {
                CCL_LOG_ERROR(LogCategory::Editor, "Failed to launch Tracy. Verify path: Tools\\Tracy\\tracy-profiler.exe");
            }
            else {
                CCL_LOG_INFO(LogCategory::Editor, "Tracy Profiler launched successfully.");
            }
        }

        // 💡 将来的に RenderDoc などを追加する場合はここに追記していく
        // if (ImGui::MenuItem("Launch RenderDoc")) { ... }

        ImGui::EndMenu();
    }
}

void EditorWindowManager::SaveLayout(const std::string& path)
{
    nlohmann::json j;
    j["windows"] = nlohmann::json::array();

    for (auto& window : _windows) {
        j["windows"].push_back({
            {"name", window->GetName()},
            {"visible", window->GetVisible()}
            });
    }

    FileSystem::WriteAllText(path, j.dump(4));
    CCL_LOG_INFO(LogCategory::Editor, "Layout saved: %s", path.c_str());
}

void EditorWindowManager::LoadLayout(const std::string& path)
{
    std::string data = FileSystem::ReadAllText(path);
    if (data.empty()) return;

    try {
        auto j = nlohmann::json::parse(data);
        for (const auto& item : j["windows"]) {
            std::string name = item["name"];
            bool visible = item["visible"];

            // 名前で検索して状態を適用
            for (auto& window : _windows) {
                if (window->GetName() == name) {
                    window->SetVisible(visible);
                    break;
                }
            }
        }
        CCL_LOG_INFO(LogCategory::Editor, "Layout loaded: %s", path.c_str());
    }
    catch (...) {
        CCL_LOG_ERROR(LogCategory::Editor, "Failed to load layout.");
    }
}