#include "EditorWindowManager.h"

#include "Engine/Serialization/Serializer/SceneSerializer.h"
#include "Engine/Graphics/RHI/Graphics.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Scene/SceneFactory.h" // 登録済みシーン一覧（Sceneメニューの自動生成）
#include "Engine/Platform/Core/Dialog.h"

#include "Editor/Core/EditorCommandHistory.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/Core/App/GameFlowState.h" // カーソル自由(UI操作)モードの共有フラグ

// マウス視点の可否（全画面 or ゲームビュー フォーカス）を InputFacade へ伝えるため
#include "Engine/Core/App/EngineServices.h"
#include "Engine/Platform/Input/InputFacade.h"
#include "Engine/UI/ECS/GameViewportInfo.h" // GameView表示矩形の毎フレーム無効化

#include "Editor/Windows/General/AssetBrowserWindow.h"
#include "Editor/Windows/General/ECSDebugWindow.h"
#include "Editor/Windows/General/GameViewWindow.h"
#include "Editor/Windows/General/HierarchyWindow.h"
#include "Editor/Windows/General/InspectorWindow.h"
#include "Editor/Windows/Animation/AnimationSequencerWindow.h"
#include "Editor/Windows/Game/TextEditorWindow.h"
#include "Editor/Windows/Game/InputEditorWindow.h"
#include "Editor/Windows/General/ConsoleWindow.h"
#include "Editor/Windows/Animation/AnimGraphWindow.h"
#include "Editor/Windows/AI/BehaviorTreeWindow.h"
#include "Editor/Windows/Game/FormationEditorWindow.h"
#include "Editor/Windows/Cinematic/CinematicEditorWindow.h"
#include "Editor/Windows/General/MemoryAnalyzerWindow.h"
#include "Editor/Windows/Game/CurveEditorWindow.h"
#include "Editor/Windows/General/RenderDebugWindow.h"
#include "Editor/Windows/General/BuildingScatterWindow.h"

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
#include <algorithm> // std::sort（Sceneメニューの名前順ソート）

#include <tracy/Tracy.hpp>

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// 初期化（ウィンドウの登録）
// ---------------------------------------------------------------------------

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
    RegisterWindow<FormationEditorWindow>();
    RegisterWindow<CinematicEditorWindow>();
    RegisterWindow<CurveEditorWindow>();

    RegisterWindow<TextEditorWindow>();
    RegisterWindow<InputEditorWindow>();
    RegisterWindow<MemoryAnalyzerWindow>();
    RegisterWindow<GameViewWindow>();
    RegisterWindow<RenderDebugWindow>();
    RegisterWindow<BuildingScatterWindow>();
}

// ---------------------------------------------------------------------------
// プレハブのフォルダ階層をメニュー化するためのヘルパー
// ---------------------------------------------------------------------------
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
                    DirectX::XMFLOAT3  spawnPos = {0, 5, 0};
                    CCL::ECS::EntityID e        = Prefab::SpawnPrefab(*context.world, node.fullPath, spawnPos);
                    // プレハブの実体として印を付ける。レベル保存では {path, transform} の参照になり、
                    // シーン（ベイク）保存では除外される。
                    if (e != CCL::ECS::InvalidEntityID)
                        context.world->AddComponent<PrefabInstanceComponent>(e, PrefabInstanceComponent{node.fullPath});
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
    // Assets/Prefabs 以下の .json を集め直す
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
} // namespace

// ---------------------------------------------------------------------------
// 毎フレームの描画
// ---------------------------------------------------------------------------

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

        // TAB で一時停止・再開を切り替える（ツールバーの Pause/Resume ボタンと同じ動作）。
        //   テキスト入力欄にフォーカスがあるときは ImGui のフィールド移動を優先し、切り替えない。
        if (ImGui::IsKeyPressed(ImGuiKey_Tab, false) && !ImGui::GetIO().WantCaptureKeyboard)
        {
            context.isPaused = !context.isPaused;
        }

        // F11 で1フレームだけコマ送り（一時停止中のみ有効）。false=リピートなしで1回だけ。
        if (context.isPaused && ImGui::IsKeyPressed(ImGuiKey_F11, false) && !ImGui::GetIO().WantCaptureKeyboard)
        {
            context.stepOneFrame = true;
        }

        // F2 で「カーソル自由(UI操作)モード」を切り替える。ゲームは動いたままカーソルだけ解放し、
        //   実行中に ImGui エディタ(シーケンサー等)を操作できるようにする。テキスト入力中は無視する。
        if (ImGui::IsKeyPressed(ImGuiKey_F2, false) && !ImGui::GetIO().WantCaptureKeyboard)
        {
            context.cursorFreeToggle = !context.cursorFreeToggle;
        }

        // 「カーソル自由」の最終状態を World リソースへ反映する。
        //   F2 が ON か、シーケンサーの Edit Mode 中なら true。カメラ(カーソルロック)と
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

    // ゲーム画像の表示矩形も毎フレーム無効化。GameView が描いたフレームだけ再び valid=true になる。
    //
    // editorVisible も併せて書く。valid=false には
    //   「全画面/Shipping なので恒等写像でよい」と
    //   「エディタは出ているが GameView が隠れていてゲーム画面が無い」
    // の2つが混ざっており、後者で恒等写像に落とすと、画面に出ていないボタンを
    // 他のウィンドウ上のクリックが押してしまう（GameViewportInfo.h の説明を参照）。
    if (context.world && context.world->HasResource<GameViewportInfo>())
    {
        auto &vp         = context.world->GetResource<GameViewportInfo>();
        vp.valid         = false;
        vp.editorVisible = m_isVisible;
        vp.keyboardFocus = false; // GameView がフォーカスを持つフレームだけ書き直す
    }

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

// ---------------------------------------------------------------------------
// メニューバー
// ---------------------------------------------------------------------------

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
        if (ImGui::MenuItem("Open Scene..."))
        {
            HWND hWnd = Graphics::Instance().GetWindowHandle();
            if (auto path = FileDialog::OpenPreset("Scene", "Open Scene", hWnd))
            {
                context.pendingLoadScenePath = path->string();
                context.ClearSelection();
            }
        }
        if (ImGui::MenuItem("Save Scene"))
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
        // world が無い（シーン未ロード）間は生成できない。他のメニューは確認している。
        ImGui::BeginDisabled(context.world == nullptr);
        if (ImGui::MenuItem("Add Empty Entity"))
        {
            Archetype arch = ArchetypeHelper::Generate<TransformComponent>();
            context.world->RequestSpawnEntity(arch);
        }
        ImGui::EndDisabled();
        ImGui::EndMenu();
    }
}

void EditorWindowManager::DrawPrefabMenu(EditorContext &context)
{
    ZoneScopedN("EditorWindowManager::DrawPrefabMenu");
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
            // パスを "/" で区切ってフォルダの木を組み、サブメニューとして出す
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
        auto &sceneMgr = SceneManager::Instance();

        // SceneFactory へ REGISTER_SCENE で自己登録された全シーンをそのまま列挙する。
        //   シーンを追加しても、このメニューへ手で書き足す必要はない。
        //   登録先は unordered_map で順序が不定なので、表示は名前順にソートして安定させる。
        std::vector<std::string> sceneNames = SceneFactory::Instance().GetSceneNames();
        std::sort(sceneNames.begin(), sceneNames.end());

        const std::string &currentName   = sceneMgr.GetCurrentSceneName();
        const bool         transitioning = sceneMgr.IsTransitioning();

        if (transitioning)
            ImGui::TextDisabled("遷移中...");

        for (const std::string &name : sceneNames)
        {
            const bool isCurrent = (name == currentName);

            // 現在のシーンはチェックを付け、選べないようにする（自分自身への遷移を防ぐ）。
            // 遷移中も多重遷移を避けるため全項目を無効化する。
            ImGui::BeginDisabled(isCurrent || transitioning);
            if (ImGui::MenuItem(name.c_str(), nullptr, /*selected*/ isCurrent))
            {
                // 全シーン共通でフェード付きの非同期遷移にする。
                //   RequestTransition(async=true): 暗転 → 暗転中に対象シーンを別スレッドで読み込む
                //   → 読み込み完了後に1回だけ明転。メインスレッドを止めない。
                sceneMgr.RequestTransition(name, /*async*/ true);

                // 遷移先では World もエンティティも作り直される。古い EntityID を
                // 掴んだままだと、暗転中の数フレーム Inspector が無効な対象を描く。
                //   （Undo 履歴の破棄は切替が実際に起きた瞬間に Framework が行う）
                context.ClearSelection();
            }
            ImGui::EndDisabled();
        }

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

        // 触っていない外部ウィンドウの描画を約15fpsまで落とす。別画面の数値を
        //   滑らかに眺めたいときだけ OFF にする（そのぶんFPSは下がる）。
        ImGui::MenuItem("外部ウィンドウを間引く", nullptr, &context.throttleExternalWindows);

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

// ---------------------------------------------------------------------------
// 再生操作と統計の帯
// ---------------------------------------------------------------------------

void EditorWindowManager::DrawPlayControlBar(EditorContext &context)
{
    ZoneScopedN("EditorWindowManager::DrawPlayControlBar");

    // 一時停止・再開の切り替えだけを行う。
    //   セーブやシーンのバックアップ・復元は行わない。押した瞬間に
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

    // デバッグ速度（スロー再生）の切り替え。
    //   等速以外のときは戻し忘れないよう、選択中の表示を色付きにする。
    struct SpeedChoice
    {
        const char *label;
        float       speed;
    };
    static constexpr SpeedChoice kSpeeds[] = {
        {"等速", 1.0f},
        {"1/2", 0.5f},
        {"1/4", 0.25f},
        {"1/10", 0.1f},
    };

    const char *current = kSpeeds[0].label;
    for (const auto &choice : kSpeeds)
        if (context.debugSpeed == choice.speed) current = choice.label;

    ImGui::SameLine();
    ImGui::SetNextItemWidth(70.0f);
    const bool slowed = (context.debugSpeed != 1.0f);
    if (slowed) ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.6f, 0.35f, 0.1f, 1.0f));
    if (ImGui::BeginCombo("##DebugSpeed", current))
    {
        for (const auto &choice : kSpeeds)
            if (ImGui::Selectable(choice.label, context.debugSpeed == choice.speed))
                context.debugSpeed = choice.speed;
        ImGui::EndCombo();
    }
    if (slowed) ImGui::PopStyleColor();
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

        // 直近120回ぶんのフレーム時間（ツールチップを出している間だけ記録する）
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

// ---------------------------------------------------------------------------
// ツールメニュー
// ---------------------------------------------------------------------------

void EditorWindowManager::DrawToolsMenu(EditorContext &context)
{
    ZoneScopedN("EditorWindowManager::DrawToolsMenu");
    if (ImGui::BeginMenu("Tools"))
    {
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

// ---------------------------------------------------------------------------
// レイアウトの保存・読み込み
// ---------------------------------------------------------------------------

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