#include "EditorWindowManager.h"

#include "Engine/Serialization/Serializer/SceneSerializer.h"
#include "Engine/Graphics/RHI/Graphics.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Platform/Core/Dialog.h"

#include "Editor/Core/EditorCommandHistory.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/Core/App/GameFlowState.h" // カーソル自由(UI操作)モードの共有フラグ

// マウス視点の可否（全画面 or ゲームビュー フォーカス）を InputFacade へ伝えるため
#include "Engine/Core/App/EngineServices.h"
#include "Engine/Platform/Input/InputFacade.h"
#include "Engine/UI/ECS/GameViewportInfo.h" // GameView表示矩形の毎フレーム無効化

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
#include "Editor/Windows/TestLauncherWindow.h"
#include "Editor/Windows/CurveEditorWindow.h"
#include "Editor/Windows/CameraPathEditorWindow.h"
#include "Editor/Windows/StateTreeWindow.h"
#include "Editor/Windows/RenderDebugWindow.h"
#include "Editor/Windows/BuildingScatterWindow.h"

#include "Engine/Serialization/Factory/Prefab.h"
#include "Engine/Core/Components/PrefabInstanceComponent.h"
#include "Engine/Platform/Core/FileSystem.h"
#include <fstream>
#include <json.hpp>
#include <imgui.h>
#include <filesystem>
#include <windows.h>
#include <shellapi.h>
#include <map>
#include <sstream>

// Tracy プロファイラをインクルード
#include <tracy/Tracy.hpp>

namespace fs = std::filesystem;

void EditorWindowManager::Initialize()
{
    ZoneScopedN("EditorWindowManager::Initialize");

    RegisterWindow<InspectorWindow>();
    RegisterWindow<HierarchyWindow>();
    RegisterWindow<AssetBrowserWindow>();

    RegisterWindow<ECSDebugWindow>();
    RegisterWindow<ConsoleWindow>();
    RegisterWindow<AnimationSequencerWindow>();
    RegisterWindow<AnimGraphWindow>();
    RegisterWindow<BehaviorTreeWindow>();
    RegisterWindow<StateTreeWindow>();
    RegisterWindow<CameraPathEditorWindow>();
    RegisterWindow<CurveEditorWindow>();

    RegisterWindow<TextEditorWindow>();
    RegisterWindow<InputEditorWindow>();
    RegisterWindow<NavMeshWindow>();
    RegisterWindow<MemoryAnalyzerWindow>();
    RegisterWindow<TestLauncherWindow>();
    RegisterWindow<GameViewWindow>();
    RegisterWindow<RenderDebugWindow>();
    RegisterWindow<BuildingScatterWindow>();
}

// =========================================================
// プレハブのフォルダ階層をメニュー化するためのヘルパー
// =========================================================
namespace
{
struct PrefabNode
{
    bool                                               isFile = false;
    std::string                                        fullPath;
    std::map<std::string, std::unique_ptr<PrefabNode>> children;
};

void DrawPrefabNodeTree(const std::string &nodeName, const PrefabNode &node, EditorContext &context)
{
    if (node.isFile)
    {
        std::string displayName = std::filesystem::path(nodeName).stem().string();
        std::string menuText    = "  " + displayName;

        if (ImGui::MenuItem(menuText.c_str()))
        {
            if (context.world)
            {
                DirectX::XMFLOAT3 spawnPos = {0, 5, 0};
                CCL::ECS::EntityID e = Prefab::SpawnPrefab(*context.world, node.fullPath, spawnPos);
                // プレハブ実体としてマーク。レベル保存で {path, transform} 参照に、
                //    シーン(ベイク)保存では除外対象になる。
                if (e != CCL::ECS::InvalidEntityID)
                    context.world->AddComponent<PrefabInstanceComponent>(e, PrefabInstanceComponent{ node.fullPath });
                CCL_LOG_INFO(LogCategory::Editor, "Spawned Prefab: %s", displayName.c_str());
            }
        }
    }
    else
    {
        std::string menuText = "[+] " + nodeName;
        if (ImGui::BeginMenu(menuText.c_str()))
        {
            for (const auto &[childName, childNode] : node.children)
            {
                if (!childNode->isFile) DrawPrefabNodeTree(childName, *childNode, context);
            }
            for (const auto &[childName, childNode] : node.children)
            {
                if (childNode->isFile) DrawPrefabNodeTree(childName, *childNode, context);
            }
            ImGui::EndMenu();
        }
    }
}
} // namespace

void RefreshPrefabList(EditorContext &context)
{
    ZoneScopedN("RefreshPrefabList");

    context.prefabFiles.clear();

    std::string targetPath = "Assets/Prefabs";

    if (fs::exists(targetPath))
    {
        try
        {
            for (const auto &entry : fs::recursive_directory_iterator(targetPath))
            {
                if (entry.is_regular_file() && entry.path().extension() == ".json")
                {
                    context.prefabFiles.push_back(entry.path().generic_string());
                }
            }
        }
        catch (const fs::filesystem_error &e)
        {
            OutputDebugStringA(e.what());
        }
    }
}

void EditorWindowManager::Draw(EditorContext &context)
{
    ZoneScopedN("EditorWindowManager::Draw");

    {
        ZoneScopedN("Input Check (Undo/Redo & F1)");
        if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z))
        {
            EditorCommandHistory::Instance().Undo();
        }
        if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y))
        {
            EditorCommandHistory::Instance().Redo();
        }

        if (ImGui::IsKeyPressed(ImGuiKey_F1, false))
        {
            m_isVisible = !m_isVisible;
        }

        // TAB で一時停止/再開をトグル（ツールバーの Pause/Resume ボタンと同じ動作）。
        //   テキスト入力欄にフォーカスがあるときは ImGui のフィールド移動を優先し、誤爆させない。
        if (ImGui::IsKeyPressed(ImGuiKey_Tab, false) && !ImGui::GetIO().WantCaptureKeyboard)
        {
            context.isPaused = !context.isPaused;
        }

        // F11 で1フレームだけコマ送り（一時停止中のみ有効）。false=リピートなしで1回だけ。
        if (context.isPaused && ImGui::IsKeyPressed(ImGuiKey_F11, false) && !ImGui::GetIO().WantCaptureKeyboard)
        {
            context.stepOneFrame = true;
        }

        // F2 で「カーソル自由(UI操作)モード」をトグル。ゲームは動いたままカーソルだけ解放し、
        //   実行中に ImGui エディタ(シーケンサー等)を操作できるようにする。誤爆防止に入力中は無視。
        if (ImGui::IsKeyPressed(ImGuiKey_F2, false) && !ImGui::GetIO().WantCaptureKeyboard)
        {
            context.cursorFreeToggle = !context.cursorFreeToggle;
        }

        // 「カーソル自由」の最終状態を World リソースへ反映する。
        //   F2トグル or シーケンサーEditMode 中は true。カメラ(カーソルロック)と
        //   PauseSystem(ESC抑制)がこれを読む。
        if (context.world && context.world->HasResource<GameFlowState>())
        {
            context.world->GetResource<GameFlowState>().uiInteract =
                context.cursorFreeToggle || context.isAnimEditMode;
        }
    }

    if (context.prefabFiles.empty())
    {
        ZoneScopedN("Init: RefreshPrefabList");
        RefreshPrefabList(context);
    }

    // 今フレームのフォーカス状態はこの後 GameViewWindow が設定する。
    // 窓が閉じている等で更新されない場合に前フレームの値が残らないよう先にクリア。
    context.gameViewFocused = false;

    // ゲーム画像の表示矩形も同様に毎フレーム無効化。GameView が描いたフレームだけ再び valid=true
    // になる。全画面(F1)や窓を閉じた時は valid=false のまま → UIクリック判定は恒等写像に戻る。
    if (context.world && context.world->HasResource<GameViewportInfo>())
        context.world->GetResource<GameViewportInfo>().valid = false;

    if (!m_isVisible)
    {
        ZoneScopedN("Draw Background Canvas");

        // 全画面(F1)プレイ中は常にマウス視点を有効化する。
        if (auto &input = EngineServices::Instance().inputFacade)
            input->SetGameInputActive(true);

        if (context.gameSRV)
        {
            ImVec2 displaySize = ImGui::GetIO().DisplaySize;
            ImGui::GetBackgroundDrawList()->AddImage(
                context.gameSRV,
                ImVec2(0, 0),
                displaySize);
        }
        return;
    }

    {
        ZoneScopedN("DrawMainMenuBar");
        DrawMainMenuBar(context);
    }

    {
        ZoneScopedN("Render Windows Loop");
        for (auto &window : m_windows)
        {
            ZoneScoped;
            ZoneName(window->GetName().c_str(), window->GetName().size());
            window->Render(context);
        }
    }

    // 窓描画後の確定したフォーカス状態を InputFacade へ反映。
    //   ゲームビューにフォーカスがある時だけマウス視点を通す（編集中に視点が
    //   マウスへ引っ張られるのを防ぐ）。
    if (auto &input = EngineServices::Instance().inputFacade)
        input->SetGameInputActive(context.gameViewFocused);
}

void EditorWindowManager::DrawMainMenuBar(EditorContext &context)
{
    if (ImGui::BeginMainMenuBar())
    {
        DrawFileMenu(context);
        DrawEntityMenu(context);
        DrawPrefabMenu(context);
        DrawSceneMenu(context);
        DrawWindowMenu(context);

        DrawToolsMenu(context);

        DrawPlayControlBar(context);

        DrawPerformanceStatsBar(context);

        ImGui::EndMainMenuBar();
    }
}

void EditorWindowManager::DrawFileMenu(EditorContext &context)
{
    ZoneScopedN("EditorWindowManager::DrawFileMenu");
    if (ImGui::BeginMenu("File"))
    {
        if (ImGui::MenuItem("Open Scene...", "Ctrl+O"))
        {
            HWND hWnd = Graphics::Instance().GetWindowHandle();
            if (auto path = FileDialog::OpenPreset("Scene", "Open Scene", hWnd))
            {
                context.pendingLoadScenePath = path->string();
                context.ClearSelection();
            }
        }
        if (ImGui::MenuItem("Save Scene", "Ctrl+S"))
        {
            HWND hWnd = Graphics::Instance().GetWindowHandle();
            if (auto path = FileDialog::SavePreset("Scene", "Save Scene", {}, hWnd))
            {
                context.pendingSaveScenePath = path->string();
                context.ClearSelection();
            }
        }

        ImGui::Separator();

        // ---- レベル = ステージ(ベイク) + プレハブ実体(参照配置) ----
        //   Open Scene/Save Scene が「ステージ層」だけを扱うのに対し、こちらは
        //   プレハブ配置(プレイヤー/敵など)を参照ごと一括で保存/復元する。
        if (ImGui::MenuItem("Open Level..."))
        {
            HWND hWnd = Graphics::Instance().GetWindowHandle();
            if (auto path = FileDialog::OpenPreset("Scene", "Open Level", hWnd))
            {
                context.pendingLoadLevelPath = path->string();
                context.ClearSelection();
            }
        }
        if (ImGui::MenuItem("Save Level..."))
        {
            HWND hWnd = Graphics::Instance().GetWindowHandle();
            if (auto path = FileDialog::SavePreset("Scene", "Save Level", {}, hWnd))
            {
                context.pendingSaveLevelPath = path->string();
            }
        }

        ImGui::Separator();
        if (ImGui::MenuItem("Exit"))
        {
            PostQuitMessage(0);
        }
        ImGui::EndMenu();
    }
}

void EditorWindowManager::DrawEntityMenu(EditorContext &context)
{
    ZoneScopedN("EditorWindowManager::DrawEntityMenu");
    if (ImGui::BeginMenu("Entity"))
    {
        if (ImGui::MenuItem("Add Empty Entity"))
        {
            Archetype arch = ArchetypeHelper::Generate<TransformComponent>();
            context.world->RequestSpawnEntity(arch);
        }
        ImGui::EndMenu();
    }
}

void EditorWindowManager::DrawPrefabMenu(EditorContext &context)
{
    ZoneScopedN("EditorWindowManager::DrawPrefabMenu"); // ここが重い可能性も高い
    if (ImGui::BeginMenu("Prefab"))
    {
        if (ImGui::MenuItem("Refresh List"))
        {
            RefreshPrefabList(context);
        }
        ImGui::Separator();

        if (context.prefabFiles.empty())
        {
            ImGui::TextDisabled("No prefabs found");
        }
        else
        {
            PrefabNode  rootNode;
            std::string basePrefix = "Assets/Prefabs";
            std::replace(basePrefix.begin(), basePrefix.end(), '\\', '/');

            for (const auto &pathStr : context.prefabFiles)
            {
                std::string relativePath = pathStr;
                std::replace(relativePath.begin(), relativePath.end(), '\\', '/');

                size_t pos = relativePath.find(basePrefix);
                if (pos == 0)
                {
                    relativePath = relativePath.substr(basePrefix.length());
                    if (!relativePath.empty() && relativePath[0] == '/')
                    {
                        relativePath = relativePath.substr(1);
                    }
                }

                std::stringstream ss(relativePath);
                std::string       token;
                PrefabNode       *current = &rootNode;

                std::vector<std::string> parts;
                while (std::getline(ss, token, '/'))
                {
                    parts.push_back(token);
                }

                for (size_t i = 0; i < parts.size(); ++i)
                {
                    const auto &part = parts[i];
                    if (current->children.find(part) == current->children.end())
                    {
                        current->children[part] = std::make_unique<PrefabNode>();
                    }
                    current = current->children[part].get();

                    if (i == parts.size() - 1)
                    {
                        current->isFile   = true;
                        current->fullPath = pathStr;
                    }
                }
            }

            for (const auto &[childName, childNode] : rootNode.children)
            {
                if (!childNode->isFile) DrawPrefabNodeTree(childName, *childNode, context);
            }
            for (const auto &[childName, childNode] : rootNode.children)
            {
                if (childNode->isFile) DrawPrefabNodeTree(childName, *childNode, context);
            }
        }
        ImGui::EndMenu();
    }
}

void EditorWindowManager::DrawSceneMenu(EditorContext &context)
{
    ZoneScopedN("EditorWindowManager::DrawSceneMenu");
    if (ImGui::BeginMenu("Scene"))
    {
        if (ImGui::MenuItem("Particle Scene")) SceneManager::Instance().ChangeScene("Particle");
        if (ImGui::MenuItem("Editor Scene")) SceneManager::Instance().ChangeScene("Editor");
        ImGui::Separator();
        // 非同期ロード版：ローディング画面を挟んで裏読み遷移する（動作確認用）
        if (ImGui::MenuItem("Editor Scene (Async)")) SceneManager::Instance().ChangeSceneAsync("Editor");
        ImGui::Separator();
        // ゲームループ：タイトル画面から START でゲームへ（動作確認用）
        if (ImGui::MenuItem("Title Scene")) SceneManager::Instance().ChangeScene("Title");
        ImGui::EndMenu();
    }
}

void EditorWindowManager::DrawWindowMenu(EditorContext &context)
{
    ZoneScopedN("EditorWindowManager::DrawWindowMenu");
    if (ImGui::BeginMenu("Window"))
    {
        if (ImGui::MenuItem("Show UI", "F1", &m_isVisible))
        {
        }

        // 切り離した OS ウィンドウ(別画面に出した ImGui)を実際に描画するか。
        //   OFF にすると Exe 外のウィンドウは黒くなる代わりに負荷が下がる。
        ImGui::MenuItem("外部ウィンドウを描画", nullptr, &context.renderExternalWindows);

        ImGui::Separator();
        for (auto &window : m_windows)
        {
            bool open = window->GetVisible();
            if (ImGui::MenuItem(window->GetName().c_str(), nullptr, &open))
            {
                window->SetVisible(open);
            }
        }
        ImGui::EndMenu();
    }
}

void EditorWindowManager::DrawPlayControlBar(EditorContext &context)
{
    ZoneScopedN("EditorWindowManager::DrawPlayControlBar");

    // 「一時停止 / 再開」トグルのみ。
    //   セーブやシーンのバックアップ/復元は一切行わない。押した瞬間に
    //   context.isPaused を反転するだけで、Framework 側がその間 FixedUpdate を止める。
    float  menuBarWidth = ImGui::GetWindowWidth();
    float  buttonWidth  = 90.0f;
    ImVec2 btnSize      = ImVec2(buttonWidth, 0);

    ImGui::SameLine(menuBarWidth * 0.5f - buttonWidth * 0.5f);

    if (context.isPaused)
    {
        if (ImGui::Button("> Resume", btnSize))
        {
            context.isPaused = false;
        }
        // 停止中のみコマ送りボタンを出す（F11 と同じ動作）
        ImGui::SameLine();
        if (ImGui::Button(">| Step", btnSize))
        {
            context.stepOneFrame = true;
        }
    }
    else
    {
        if (ImGui::Button("|| Pause", btnSize))
        {
            context.isPaused = true;
        }
    }
}

void EditorWindowManager::DrawPerformanceStatsBar(EditorContext &context)
{
    ZoneScopedN("EditorWindowManager::DrawPerformanceStatsBar");
    ImGuiIO &io  = ImGui::GetIO();
    float    fps = io.Framerate;
    float    ms  = 1000.0f / (fps > 0.0f ? fps : 1.0f);

    char textBuffer[128];
    snprintf(textBuffer, sizeof(textBuffer), "%.1f FPS (%.2f ms)", fps, ms);

    float textWidth   = ImGui::CalcTextSize(textBuffer).x;
    float rightMargin = 20.0f;

    ImGui::SameLine(ImGui::GetWindowWidth() - textWidth - rightMargin);

    ImVec4 color;
    if (fps >= 55.0f)
        color = ImVec4(0.4f, 1.0f, 0.4f, 1.0f);
    else if (fps >= 30.0f)
        color = ImVec4(1.0f, 1.0f, 0.4f, 1.0f);
    else
        color = ImVec4(1.0f, 0.4f, 0.4f, 1.0f);

    ImGui::TextColored(color, "%s", textBuffer);

    if (ImGui::IsItemHovered())
    {
        ImGui::BeginTooltip();
        ImGui::Text("Performance & ECS Status");
        ImGui::Separator();

        static float frameTimes[120] = {0};
        static int   frameTimeIdx    = 0;

        frameTimes[frameTimeIdx] = ms;
        frameTimeIdx             = (frameTimeIdx + 1) % 120;

        ImGui::PlotLines("Frame Time(ms)", frameTimes, 120, frameTimeIdx, "ms", 0.0f, 33.3f, ImVec2(200, 50));

        if (context.world)
        {
            ImGui::Spacing();

            size_t totalEntities = 0;
            size_t activeChunks  = 0;
            for (const auto &chunk : context.world->GetChunkManager().GetChunks())
            {
                if (chunk)
                {
                    totalEntities += chunk->GetEntityCount();
                    activeChunks++;
                }
            }
            ImGui::TextDisabled("Active Entities:");
            ImGui::SameLine();
            ImGui::Text("%zu", totalEntities);
            ImGui::TextDisabled("Active Chunks:");
            ImGui::SameLine();
            ImGui::Text("%zu", activeChunks);
        }
        ImGui::EndTooltip();
    }
}

void EditorWindowManager::DrawToolsMenu(EditorContext &context)
{
    ZoneScopedN("EditorWindowManager::DrawToolsMenu");
    if (ImGui::BeginMenu("Tools"))
    {
        // Tracy Profiler を起動するメニュー項目
        if (ImGui::MenuItem("Launch Tracy Profiler"))
        {
            HINSTANCE result = ShellExecuteA(
                nullptr,
                "open",
                "Tools\\Tracy\\tracy-profiler.exe",
                nullptr,
                nullptr,
                SW_SHOW);

            if ((INT_PTR)result <= 32)
            {
                CCL_LOG_ERROR(LogCategory::Editor, "Failed to launch Tracy. Verify path: Tools\\Tracy\\tracy-profiler.exe");
            }
            else
            {
                CCL_LOG_INFO(LogCategory::Editor, "Tracy Profiler launched successfully.");
            }
        }

        // ModelEditor を起動するメニュー項目
        if (ImGui::MenuItem("Launch ModelEditor"))
        {
            HINSTANCE result = ShellExecuteA(
                nullptr,
                "open",
                "Tools\\ModelEditor\\ModelEditor.exe",
                nullptr,
                nullptr,
                SW_SHOW);

            if ((INT_PTR)result <= 32)
            {
                CCL_LOG_ERROR(LogCategory::Editor, "Failed to launch ModelEditor. Verify path: Tools\\ModelEditor\\ModelEditor.exe");
            }
            else
            {
                CCL_LOG_INFO(LogCategory::Editor, "ModelEditor launched successfully.");
            }
        }



        ImGui::EndMenu();
    }

}

void EditorWindowManager::SaveLayout(const std::string &path)
{
    ZoneScopedN("EditorWindowManager::SaveLayout");
    nlohmann::json j;
    j["windows"] = nlohmann::json::array();

    for (auto &window : m_windows)
    {
        j["windows"].push_back({{"name", window->GetName()},
            {"visible", window->GetVisible()}});
    }

    FileSystem::WriteAllText(path, j.dump(4));
    CCL_LOG_INFO(LogCategory::Editor, "Layout saved: %s", path.c_str());
}

void EditorWindowManager::LoadLayout(const std::string &path)
{
    ZoneScopedN("EditorWindowManager::LoadLayout");
    std::string data = FileSystem::ReadAllText(path);
    if (data.empty()) return;

    try
    {
        auto j = nlohmann::json::parse(data);
        for (const auto &item : j["windows"])
        {
            std::string name    = item["name"];
            bool        visible = item["visible"];

            for (auto &window : m_windows)
            {
                if (window->GetName() == name)
                {
                    window->SetVisible(visible);
                    break;
                }
            }
        }
        CCL_LOG_INFO(LogCategory::Editor, "Layout loaded: %s", path.c_str());
    }
    catch (...)
    {
        CCL_LOG_ERROR(LogCategory::Editor, "Failed to load layout.");
    }
}