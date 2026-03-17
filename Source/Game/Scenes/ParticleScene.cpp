#include "ParticleScene.h"
#include "Engine/Serialization/Factory/Prefab.h"
#include "Engine/Graphics/Core/Graphics.h"
#include <imgui.h>
#include "Engine/Serialization/Factory/SceneInitializer.h"

void ParticleScene::Initialize()
{
    BaseScene::Initialize();

    _windowManager = std::make_unique<EditorWindowManager>();
    _windowManager->Initialize();

    ID3D11Device *device = Graphics::Instance().GetDevice();
    SceneInitializer::SpawnLightStartSet(*_worldPtr, device);

    EntityID camera = Prefab::SpawnPrefab(*_worldPtr, "Assets/GamePlay/Prefabs/Camera/FreeCamera");



    // ParticleScene固有: 特定のパーティクルを生成
    Prefab::SpawnPrefab(*_worldPtr, "Assets/GamePlay/Prefabs/Particles/VFX_Scene_Start");
}

void ParticleScene::Finalize()
{
    // システム破棄の前にパーティクルを破棄しないとクラッシュするため
    _particleRenderer.reset();

    BaseScene::Finalize(); 
}

void ParticleScene::RegisterSystems()
{
    // EditorSceneと全く同じ (SystemGroupsのおかげで短縮)
    ID3D11Device *device = Graphics::Instance().GetDevice();

    // SystemManagerへのアクセスを _ecsContext 経由に変更
    // (SystemGroupsの関数が参照を受け取る仕様であると推測されるため、ポインタをデリファレンスしています)
    auto &sysManager = *(_ecsContext->GetSystemManager());

    //_particleUpdater
}

void ParticleScene::OnRender3D(RenderContext &rc)
{
    auto* prim = Graphics::Instance().GetPrimitiveRenderer();
    prim->DrawGrid(20, 1);


}

void ParticleScene::OnDrawImGui()
{
    EditorContext context;
    context.selectedEntity = selectedEntity;
    context.world          = _worldPtr;
    context.systemManager  = _ecsContext->GetSystemManager(); 

    _windowManager->Draw(context);

    if (!context.pendingLoadScenePath.empty()) {
        _pendingLoadScenePath = context.pendingLoadScenePath;
    }

    if (_windowManager->IsVisible()) {
        ImGui::Begin("System Info");
        _ecsContext->GetSystemManager()->OnGui();
        ImGui::End();
    }

}

void ParticleScene::OnPreLoadScene()
{
    _particleRenderer.reset();
    selectedEntity = 0;
}