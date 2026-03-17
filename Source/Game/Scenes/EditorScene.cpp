#include "EditorScene.h"
#include "Engine/Serialization/Factory/SceneInitializer.h"
#include "Engine/Graphics/Core/Graphics.h"
#include <imgui.h>

#include "Engine/Serialization/Factory/Prefab.h"

#include "ECS/System/CCL_SystemRegistry.h"

void EditorScene::Initialize()
{
    BaseScene::Initialize(); // 共通初期化

    // エディタ固有の初期化
    _windowManager = std::make_unique<EditorWindowManager>();
    _windowManager->Initialize();


    ID3D11Device *device = Graphics::Instance().GetDevice();
    SceneInitializer::SpawnLightStartSet(*_worldPtr, device);
    //SceneInitializer::SpawnGameLightSetting(_world, device);
    Prefab::SpawnPrefab(*_worldPtr, "Assets/GamePlay/Prefabs/Camera/FreeCamera.json");



    //SceneInitializer::SpawnTransformLoadTest(*_worldPtr, device, 100000);
    SceneInitializer::SpawnJoltTest(*_worldPtr, device);

    
}

void EditorScene::Finalize() 
{

    //delete _animationSystem;

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
    if (m_isAnimEditMode && selectedEntity != 0) {
        // ロジック更新（物理、AI、移動など）を一切行わない
        // BaseScene::FixedUpdate を呼ばずに return します。
        return;
    }

    // B. 通常プレイ中
    // 親クラスの処理を呼んで、通常通り世界を動かす
    BaseScene::FixedUpdate(dt);
}

// ★追加: Updateのオーバーライド
void EditorScene::Update(float dt)
{
    // A. アニメーション編集モード中 (かつエンティティ選択中)
    if (m_isAnimEditMode && selectedEntity != 0) {

        // 1. ゲームロジックは止める (dt = 0.0f で更新)
        BaseScene::Update(0.0f);

        // 2. アニメーションだけ手動更新
        if (_animationSystem) {
            // AnimationSystem.h に UpdateManual 関数を追加済みであること
            _animationSystem->UpdateManual(selectedEntity, m_animEditTime);
        }
    }
    // B. 通常プレイ中
    else {
        BaseScene::Update(dt);
    }
}

// 3D描画の拡張 (BaseScene::Renderの途中で呼ばれる)
void EditorScene::OnRender3D(RenderContext &rc){}


// GUI描画の拡張 (BaseScene::DrawGUIから呼ばれる)
void EditorScene::OnDrawImGui()
{
    EditorContext context;
    context.selectedEntity = selectedEntity;
    context.world          = _worldPtr;
    context.systemManager  = _ecsContext->GetSystemManager(); // ★追加

    // 実体へのポインタを渡す
    context.isAnimEditMode = &m_isAnimEditMode;
    context.animEditTime   = m_animEditTime;

    _windowManager->Draw(context);

    // ロード要求があればBaseSceneのパスに渡す
    if (!context.pendingLoadScenePath.empty()) {
        _pendingLoadScenePath = context.pendingLoadScenePath;
    }

    if (_windowManager->IsVisible()) {
        ImGui::Begin("System Info");
        _ecsContext->GetSystemManager()->OnGui();
        ImGui::End();
    }
}

// ロード前の後始末
void EditorScene::OnPreLoadScene()
{
    selectedEntity = 0;
}