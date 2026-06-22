#include "Editor/Windows/TestLauncherWindow.h"
#include "Engine/Serialization/Factory/SceneInitializer.h"
#include "Engine/Serialization/Factory/Prefab.h"
#include "Engine/Graphics/RHI/Graphics.h"
#include "Engine/Transform/Systems/TransformUpdateSystem.h"
#include <imgui.h>

TestLauncherWindow::TestLauncherWindow() : EditorWindow("Test Launcher")
{
    // =======================================================
    // テストの登録 (ハードコードはここだけで済む)
    // =======================================================

    // 1. 基本テスト
    _basicTests.push_back({ "Jolt Physics Basic", SceneInitializer::SpawnJoltTest });
    _basicTests.push_back({ "Animation Basic", SceneInitializer::SpawnAnimationTest });
    _basicTests.push_back({ "Shader Test", SceneInitializer::SpawnShaderTest });

    // 2. パフォーマンステスト (名前, デフォルトのCount, 関数)
    _perfTests.push_back({ "Galaxy Swarm", 100000, SceneInitializer::SpawnECSBenchmarkTest });
    _perfTests.push_back({ "Voxel Ocean", 400, SceneInitializer::SpawnVoxelOceanTest });

    _perfTests.push_back({ "Model Grid", 500, SceneInitializer::SpawnModelGridTest });

   
}

void TestLauncherWindow::CleanupAndInitializeEnv(EditorContext& context)
{
    auto* world = context.world;
    auto* device = Graphics::Instance().GetDevice();

    auto& chunks = world->GetChunkManager().GetChunks();
    for (auto& chunkPtr : chunks) {
        if (!chunkPtr) continue;
        auto* chunk = chunkPtr.get();

        for (size_t i = 0; i < chunk->GetEntityCount(); ++i) {
            CCL::ECS::EntityID entity = chunk->GetEntityByIndex(i);
            if (entity != CCL::ECS::InvalidEntityID) {
                // 階層ごと安全に削除予約（DestroyTag を付与）
                TransformUpdateSystem::MarkForDestruction(*world, entity, true);
            }
        }
    }

    world->ScrutinyAndApply();

    // 2. 必須環境の再構築 (これらは絶対に消えない)
    SceneInitializer::SpawnLightStartSet(*world, device);
    Prefab::SpawnPrefab(*world, "Assets/Prefabs/Camera/FreeCamera.json");
}

void TestLauncherWindow::DrawContents(EditorContext& context)
{
    auto* world = context.world;
    auto* device = Graphics::Instance().GetDevice();

    ImGui::TextWrapped("Select a test. Environment will be reset, preserving Camera and Lights.");
    ImGui::Spacing();

    // =======================================================
    // カテゴリ1: パフォーマンステスト (Count付き)
    // =======================================================
    if (ImGui::CollapsingHeader("Performance Tests", ImGuiTreeNodeFlags_DefaultOpen)) {
        for (auto& test : _perfTests) {
            ImGui::PushID(test.name.c_str()); // ID衝突防止

            if (ImGui::Button("Launch", ImVec2(80, 0))) {
                CleanupAndInitializeEnv(context);
                test.callback(*world, device, test.count); // 入力された数値を渡す
                world->ScrutinyAndApply();
            }
            ImGui::SameLine();

            // ★ 個数を弄れるスライダー
            ImGui::SetNextItemWidth(120.0f);
            ImGui::DragInt("Count", &test.count, 10.0f, 1, 1000000);

            ImGui::SameLine();
            ImGui::Text(" | %s", test.name.c_str());
            ImGui::PopID();
        }
    }

    // =======================================================
    // カテゴリ3: 基本テスト (パラメータなし)
    // =======================================================
    if (ImGui::CollapsingHeader("Graphics & Physics Tests", ImGuiTreeNodeFlags_DefaultOpen)) {
        for (auto& test : _basicTests) {
            ImGui::PushID(test.name.c_str());
            if (ImGui::Button("Launch", ImVec2(80, 0))) {
                CleanupAndInitializeEnv(context);
                test.callback(*world, device);
                world->ScrutinyAndApply();
            }
            ImGui::SameLine();
            ImGui::Text("%s", test.name.c_str());
            ImGui::PopID();
        }
    }
}