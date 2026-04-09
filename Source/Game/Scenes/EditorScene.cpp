#include "EditorScene.h"
#include "Engine/Serialization/Factory/SceneInitializer.h"
#include "Engine/Graphics/Core/Graphics.h"
#include <imgui.h>

#include "Engine/Serialization/Factory/Prefab.h"

#include "Engine/Platform/Logger.h"

#include "ECS/System/CCL_SystemRegistry.h"

#include "Editor/Utils/NavMeshBuilder.h"

void EditorScene::Initialize()
{


    BaseScene::Initialize(); // 共通初期化

    // エディタ固有の初期化
    _windowManager = std::make_unique<EditorWindowManager>();
    _windowManager->Initialize();


    ID3D11Device *device = Graphics::Instance().GetDevice();
    SceneInitializer::SpawnLightStartSet(*_worldPtr, device);
    //SceneInitializer::SpawnGameLightSetting(_world, device);
    //Prefab::SpawnPrefab(*_worldPtr, "Assets/Prefabs/Camera/FreeCamera.json");
    //Prefab::SpawnPrefab(*_worldPtr, "Assets/Prefabs/Camera/FreeFastCamera.json");


    //SceneInitializer::SpawnTransformLoadTest(*_worldPtr, device, 100000);
    //SceneInitializer::SpawnJoltTest(*_worldPtr, device);

    // 中では普通にPBRモデル用にしている
    SceneInitializer::SpawnAnimationTest(*_worldPtr, device);
    
    //SceneInitializer::SpawnPlayerCarTest(*_worldPtr, device);

    //SceneInitializer::SpawnShaderTest(*_worldPtr, device);

    // 例：10万個のキューブによる銀河の渦を生成
    //SceneInitializer::SpawnECSBenchmarkTest(*_worldPtr, device, 100000);

    // 300 x 300 = 90,000 個のキューブの海を生成
    //SceneInitializer::SpawnVoxelOceanTest(*_worldPtr, device, 300);
   
    //SceneInitializer::SpawnPhysicsAvalancheTest(*_worldPtr, device, 10000);

    // 1000体の群衆ストレステストを実行！
    //SceneInitializer::SpawnEnemySwarmTest(*_worldPtr, device, 200);


    CCL_LOG_INFO(LogCategory::Core, "EditorScene Initialize");
}

void EditorScene::Finalize() 
{

    BaseScene::Finalize(); // 共通終了化

}

void EditorScene::RegisterSystems()
{
    ID3D11Device *device = Graphics::Instance().GetDevice();

    // SystemManagerへのアクセスを _ecsContext 経由に変更
    // (SystemGroupsの関数が参照を受け取る仕様であると推測されるため、ポインタをデリファレンスしています)
    auto &sysManager = *(_ecsContext->GetSystemManager());
    CCL::ECS::SystemRegistry::Instance().RegisterAll(sysManager);

    // AnimationSystemを取得しておく (SystemGroups内で登録されている前提)
    _animationSystem = sysManager.GetSystem<AnimationSystem>();
}

void EditorScene::FixedUpdate(float dt) 
{
    // A. アニメーション編集モード中
    if (_context.isAnimEditMode && _context.selectedEntity != 0 &&
        _context.selectedEntity != CCL::ECS::InvalidEntityID) 
    {
        // ロジック更新（物理、AI、移動など）を一切行わない
        // BaseScene::FixedUpdate を呼ばずに return します。
        return;
    }

    // B. 通常プレイ中
    // 親クラスの処理を呼んで、通常通り世界を動かす
    BaseScene::FixedUpdate(dt);
}

// Updateのオーバーライド
void EditorScene::Update(float dt)
{



	// Contextからのリクエストを処理（ロード/セーブ要求など）
    ProcessContextRequests();

    // A. アニメーション編集モード中 (かつエンティティ選択中)
    if (_context.isAnimEditMode && _context.selectedEntity != 0 &&
        _context.selectedEntity != CCL::ECS::InvalidEntityID) {
        // 1. ゲームロジックは止める
        BaseScene::Update(0.0f);

        // 2. アニメーションだけ手動更新
        if (_animationSystem) {
            _animationSystem->UpdateManual(_context.selectedEntity, _context.animEditTime);
        }
    }
    // B. 通常プレイ中
    else {
        BaseScene::Update(dt);
    }
}



// GUI描画の拡張 (BaseScene::DrawGUIから呼ばれる)
void EditorScene::OnDrawImGui()
{
    _context.world         = _worldPtr;
    _context.systemManager = _ecsContext->GetSystemManager();



    ImGui::SeparatorText("NavMesh Bake Settings");

    // 構造体のインスタンスをstatic（またはEditorクラスのメンバ変数）として保持しておく
    static NavMeshBuildSettings navSettings;

    // AAAエンジンのように、主要なパラメータをスライダーで調整できるようにする
    ImGui::DragFloat("Agent Radius (太さ)", &navSettings.agentRadius, 0.05f, 0.1f, 5.0f);
    ImGui::DragFloat("Agent Height (身長)", &navSettings.agentHeight, 0.1f, 0.1f, 5.0f);
    ImGui::DragFloat("Max Climb (段差)", &navSettings.agentMaxClimb, 0.05f, 0.0f, 2.0f);
    ImGui::DragFloat("Max Slope (斜面角度)", &navSettings.agentMaxSlope, 1.0f, 0.0f, 90.0f);

    if (ImGui::Button("Bake NavMesh", ImVec2(-1, 30))) {
        // 変更した設定を渡してBake実行！
        NavMeshBuilder::BuildNavMesh(*_worldPtr, navSettings);
    }


    _windowManager->Draw(_context);

    // ロード要求があればBaseSceneのパスに渡す
    if (!_context.pendingLoadScenePath.empty()) {
        _pendingLoadScenePath = _context.pendingLoadScenePath;
    }

    if (_windowManager->IsVisible()) {
        ImGui::Begin("System Info");
        _ecsContext->GetSystemManager()->OnGui();
        ImGui::End();
    }
}

// =========================================================
// UIからのリクエスト（予約）を処理する専用関数
// =========================================================
void EditorScene::ProcessContextRequests()
{
    if (!_context.pendingLoadScenePath.empty()) {
        RequestLoadScene(_context.pendingLoadScenePath);
        _context.pendingLoadScenePath.clear();
    }

    if (!_context.pendingSaveScenePath.empty()) {
        RequestSaveScene(_context.pendingSaveScenePath);
        _context.pendingSaveScenePath.clear();
    }
}