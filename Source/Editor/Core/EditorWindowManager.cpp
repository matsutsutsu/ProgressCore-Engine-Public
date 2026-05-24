#include "EditorWindowManager.h"

#include "Engine/Serialization/SceneSerializer.h"
#include "Engine/Graphics/Core/Graphics.h"
#include "Engine/GamePlay/Core/Scene/SceneManager.h"
#include "Engine/Platform/Dialog.h"

#include "Editor/Core/EditorCommandHistory.h"

#include "Engine/GamePlay/Transform/TransformComponent.h"

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

#include "Engine/Serialization/Factory/Prefab.h"
#include <imgui.h>
#include <filesystem>

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
    //RegisterWindow<AnimGraphWindow>()->SetVisible(false);
	//RegisterWindow<BehaviorTreeWindow>();

    // ※まだ実装中で完全に読み込みたくない（クラッシュする等の）場合は、
    // 引き続きコメントアウトで対応してください。
    RegisterWindow<TextEditorWindow>()->SetVisible(false);
    RegisterWindow<InputEditorWindow>()->SetVisible(false);
    RegisterWindow<NavMeshWindow>()->SetVisible(false);
    //RegisterWindow<SceneViewWindow>();
    RegisterWindow<GameViewWindow>();
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
     // ★ 修正: F1非表示時は、背景キャンバスにゲーム画面をフルスクリーン描画する
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
        ImGui::SetNextWindowPos(ImVec2(10, 10));
        ImGui::SetNextWindowBgAlpha(0.3f);
        ImGui::Begin("HiddenOverlay", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav);
        ImGui::TextColored(ImVec4(1, 1, 0, 1), "[F1] Show Editor UI");
        ImGui::End();

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
        DrawPlayControlBar(context);

        ImGui::EndMainMenuBar();
    }
}


void EditorWindowManager::DrawFileMenu(EditorContext& context)
{
    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("Open Scene...", "Ctrl+O")) {
            char filename[256] = { 0 };
            HWND hWnd = Graphics::Instance().GetWindowHandle();
            static const char* filter = "Scene Files (*.json)\0*.json\0All Files (*.*)\0*.*\0";
            if (Dialog::OpenFileName(filename, 256, filter, "Open Scene", hWnd) == DialogResult::OK) {
                context.pendingLoadScenePath = std::string(filename);
                context.selectedEntity = CCL::ECS::InvalidEntityID;
            }
        }
        if (ImGui::MenuItem("Save Scene", "Ctrl+S")) {
            char filename[256] = {};
            HWND hWnd = Graphics::Instance().GetWindowHandle();
            static const char* filter = "Scene Files (*.json)\0*.json\0All Files (*.*)\0*.*\0";
            if (Dialog::SaveFileName(filename, 256, filter, "Save Scene", "json", hWnd) == DialogResult::OK) {
                context.pendingSaveScenePath = std::string(filename);
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
            for (const auto& pathStr : context.prefabFiles) {
                std::string filename = std::filesystem::path(pathStr).filename().string();
                if (ImGui::MenuItem(filename.c_str())) {
                    if (context.world) {
                        DirectX::XMFLOAT3 spawnPos = { 0, 5, 0 };
                        Prefab::SpawnPrefab(*context.world, pathStr, spawnPos);
                    }
                }
            }
        }
        ImGui::EndMenu();
    }
}

void EditorWindowManager::DrawSceneMenu(EditorContext& context)
{
    if (ImGui::BeginMenu("Scene")) {
        if (ImGui::MenuItem("Title Scene")) SceneManager::Instance().ChangeScene("Title");
        if (ImGui::MenuItem("Game Scene")) SceneManager::Instance().ChangeScene("Game");
        if (ImGui::MenuItem("Particle Scene")) SceneManager::Instance().ChangeScene("Particle");
        if (ImGui::MenuItem("Editor Scene")) SceneManager::Instance().ChangeScene("Editor");
        if (ImGui::MenuItem("Shop Scene")) SceneManager::Instance().ChangeScene("Shop");
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