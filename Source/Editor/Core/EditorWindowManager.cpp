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
#include "Editor/Windows/TestLauncherWindow.h"
#include "Editor/Windows/CurveEditorWindow.h"
#include "Editor/Windows/CameraPathEditorWindow.h"
#include "Editor/Windows/StateTreeWindow.h"

#include "Engine/Serialization/Factory/Prefab.h"
#include "Engine/Platform/Core/FileSystem.h"
#include <fstream>
#include <json.hpp>
#include <imgui.h>
#include <filesystem>
#include <windows.h>
#include <shellapi.h>
#include <map>
#include <sstream>

// ★ Tracy プロファイラをインクルード
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
}

// =========================================================
// ★ プレハブのフォルダ階層をメニュー化するためのヘルパー
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
                Prefab::SpawnPrefab(*context.world, node.fullPath, spawnPos);
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
            _isVisible = !_isVisible;
        }
    }

    if (context.prefabFiles.empty())
    {
        ZoneScopedN("Init: RefreshPrefabList");
        RefreshPrefabList(context);
    }

    if (!_isVisible)
    {
        ZoneScopedN("Draw Background Canvas");

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
        for (auto &window : _windows)
        {
            ZoneScoped;
            ZoneName(window->GetName().c_str(), window->GetName().size());
            window->Render(context);
        }
    }
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
                context.selectedEntity       = CCL::ECS::InvalidEntityID;
            }
        }
        if (ImGui::MenuItem("Save Scene", "Ctrl+S"))
        {
            HWND hWnd = Graphics::Instance().GetWindowHandle();
            if (auto path = FileDialog::SavePreset("Scene", "Save Scene", {}, hWnd))
            {
                context.pendingSaveScenePath = path->string();
                context.selectedEntity       = CCL::ECS::InvalidEntityID;
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
        ImGui::EndMenu();
    }
}

void EditorWindowManager::DrawWindowMenu(EditorContext &context)
{
    ZoneScopedN("EditorWindowManager::DrawWindowMenu");
    if (ImGui::BeginMenu("Window"))
    {
        if (ImGui::MenuItem("Show UI", "F1", &_isVisible))
        {
        }
        ImGui::Separator();
        for (auto &window : _windows)
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
    float menuBarWidth      = ImGui::GetWindowWidth();
    float buttonWidth       = 50.0f;
    float totalButtonsWidth = buttonWidth * 2.0f + ImGui::GetStyle().ItemSpacing.x;

    ImGui::SameLine(menuBarWidth * 0.5f - totalButtonsWidth * 0.5f);

    ImVec2 btnSize = ImVec2(buttonWidth, 0);

    if (context.playState != EditorPlayState::Stopped)
    {
        if (ImGui::Button("Stop", btnSize))
        {
            context.playState            = EditorPlayState::Stopped;
            context.pendingLoadScenePath = context.tempBackupScenePath;
            CCL_LOG_INFO(LogCategory::Editor, "Play In Editor: Stopped and Restored.");
        }
    }
    else
    {
        ImGui::BeginDisabled();
        ImGui::Button("Stop", btnSize);
        ImGui::EndDisabled();
    }

    ImGui::SameLine();

    if (context.playState == EditorPlayState::Stopped)
    {
        if (ImGui::Button("Play", btnSize))
        {
            context.pendingSaveScenePath = context.tempBackupScenePath;
            context.playState            = EditorPlayState::Playing;
            CCL_LOG_INFO(LogCategory::Editor, "Play In Editor: Started.");
        }
    }
    else if (context.playState == EditorPlayState::Playing)
    {
        if (ImGui::Button("Pause", btnSize))
        {
            context.playState = EditorPlayState::Paused;
        }
    }
    else if (context.playState == EditorPlayState::Paused)
    {
        if (ImGui::Button("Resume", btnSize))
        {
            context.playState = EditorPlayState::Playing;
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

    for (auto &window : _windows)
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

            for (auto &window : _windows)
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