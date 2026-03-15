#include "EditorScene.h"
#include "Game/Core/SystemGroups.h"
#include "Engine/Serialization/Factory/SceneInitializer.h"
#include "Game/Logics/World/LevelLoader.h"
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
    //SceneInitializer::SpawnLightStartSet(_world, device);
    //SceneInitializer::SpawnGameLightSetting(_world, device);

#if 0
    
    #if 1   敵の動きデバッグ用
    // ステージロードなど
    //LevelLoader::Load(&_world, Graphics::Instance().GetDevice(), "Assets/GamePlay/StageTemplatesDebugMaze.json");
    LevelLoader::Load(&_world, Graphics::Instance().GetDevice(), "Assets/GamePlay/StageTemplatesTestStage.json");
    SceneInitializer::SpawnMapChipTest(_world, Graphics::Instance().GetDevice());

   // ★修正: Prefab::ID::FreeCamera -> "Assets/GamePlay/Prefabs/Camera/FreeCamera.json"
    // EntityID camera = CCL::Prefab::SpawnPrefab(_world, "Assets/GamePlay/Prefabs/Camera/FreeCamera.json");

    // 敵スポーン (Input帯で処理)
    //_systemManager.RegisterSystem<EnemySpawnSystem>(SystemGroup::Logic, Priority::Logic::Input);
    //_systemManager.RegisterSystem<MapDebugSystem>(SystemGroup::Render, Priority::Render::MainDraw);

    #else   ステージテンプレート作成用

    // ステージロードなど
    LevelLoader::DebugLoad(&_world, Graphics::Instance().GetDevice(), "Assets/GamePlay/StageTemplatesDebugMaze.json");
    //SceneInitializer::SpawnMapChipTest(_world, Graphics::Instance().GetDevice());

    EntityID camera = Prefab::SpawnPrefab(
        _world, "Assets/GamePlay/Prefabs/Camera/FreeCamera.json", DirectX::XMFLOAT3{15.0f, 15.0f, 15.0f});


    // 敵スポーン (Input帯で処理)
   
    auto* mapDebug =_systemManager.RegisterSystem<MapDebugSystem>(SystemGroup::Render, Priority::Render::MainDraw);
    mapDebug->SetDevice(device);
    mapDebug->isDebugVisible = true;

    #endif
#else
    #if 0
      // ★分岐: ステージ5はボス部屋、それ以外はランダム生成

        // --- A. ボス部屋 (固定JSONロード) ---
        // 事前にMapDebugSystemで作っておいたボス部屋ファイルを指定

        LevelLoader::Load(&_world, device, "Assets/GamePlay/RoomTemplates/Stage5_BossStage.json");

         auto player = _worldPtr->View<PlayerComponent>();
         if (!player.empty()) {
        
            _worldPtr->PatchComponent<TransformComponent>(player[0], [](TransformComponent &trans) {
                trans.position = {51.0f,0.0f,12.0f}; // プレイヤーを親にする
            });
        }

        // ボス演出用ライトをスポーン
        // ★修正: Prefab::ID::BossLighting -> "Assets/GamePlay/Prefabs/Light/BossLighting.json"
        Prefab::SpawnPrefab(*_worldPtr, "Assets/GamePlay/Prefabs/Light/BossLighting.json");

        // ボスをスポーン
        // ★修正: Prefab::ID::Boss -> "Assets/GamePlay/Prefabs/Enemy/Boss.json"
        // 座標指定がない場合はJSON内の座標が使われるか、(0,0,0)になるので注意。必要なら座標引数を追加。
        Prefab::SpawnPrefab(*_worldPtr, "Assets/GamePlay/Prefabs/Enemy/Boss.json" /*, {0,0,20} */);
    #else

    Prefab::SpawnPrefab(*_worldPtr, "Assets/GamePlay/Prefabs/Camera/FreeCamera.json");
    Prefab::SpawnPrefab(*_worldPtr, "Assets/GamePlay/Prefabs/Light/LightingSetup.json");
    SceneInitializer::SpawnTransformLoadTest(*_worldPtr, device, 100000);


    #endif
       
#endif

    
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
    context.selectedEntity = &selectedEntity;
    context.world          = _worldPtr;
    context.systemManager  = _ecsContext->GetSystemManager(); // ★追加

    // 実体へのポインタを渡す
    context.isAnimEditMode = &m_isAnimEditMode;
    context.animEditTime   = &m_animEditTime;

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