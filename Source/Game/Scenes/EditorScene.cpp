#include "EditorScene.h"
#include "Engine/Scene/SceneFactory.h" // REGISTER_SCENE（ファイル末尾で自己登録）
#include "Game/Logics/Test/SceneInitializer.h"
#include "Engine/Graphics/RHI/Graphics.h"
#include "tracy/Tracy.hpp"

#include "Engine/Serialization/Factory/Prefab.h"
#include "Engine/Platform/Core/Logger.h"

#include "Engine/ECS/System/CCL_SystemRegistry.h"

#include "Game/Logics/AI/Battle/BattleContext.h" // 敵AIが参照する戦況共有リソース

#include "Game/UI/GameResultSystem.h" // ゲームオーバー検知＋リザルトUI
#include "Game/UI/PauseSystem.h"      // ESC/STARTでポーズ＋ポーズメニュー

void EditorScene::Initialize()
{
    ZoneScopedN("EditorScene::Initialize");

    BaseScene::Initialize(); // 共通初期化


    ID3D11Device *device = Graphics::Instance().GetDevice();
    {
        ZoneScopedN("EditorScene: SpawnLightStartSet");
        SceneInitializer::SpawnLightStartSet(*m_worldPtr, device);
    }
    //Prefab::SpawnPrefab(*m_worldPtr, "Assets/Prefabs/Camera/FreeCamera.json");

    // 起動コストの主因候補：テスト用エンティティ（ドローン多数など）の一括生成。
    {
        ZoneScopedN("EditorScene: SpawnGameTest");
        SceneInitializer::SpawnGameTest(*m_worldPtr, device);
    }

    //SceneManager::Instance().LoadScene(m_worldPtr,"Assets/Scene/PlayerMovementScene.json");

    // 敵AIが受け取るシーン全体の情報が入った共有のリソースを登録
    m_worldPtr->AddResource<BattleContext>();

    auto &battle           = m_worldPtr->GetResource<BattleContext>();
    battle.droneAliveCount = 650; // 動作確認用の初期値（Murielの戦術はHP割合で判定するため、この値自体は戦術選択に影響しない）


    CCL_LOG_INFO(LogCategory::Core, "EditorScene Initialize");
}

void EditorScene::Finalize() 
{
    BaseScene::Finalize(); // 共通終了化
}

void EditorScene::RegisterSystems()
{
    // SystemManagerへのアクセスを m_ecsContext 経由に変更
    // (SystemGroupsの関数が参照を受け取る仕様であると推測されるため、ポインタをデリファレンスしています)
    auto &sysManager = *(m_ecsContext->GetSystemManager());
    CCL::ECS::SystemRegistry::Instance().RegisterAll(sysManager);

    // ゲーム本編専用：ゲームオーバー検知＋リザルトUIオーバーレイ。
    // グローバル登録していないので、タイトル等には入らずゲーム世界でだけ動く。
    sysManager.RegisterSystem<GameResultSystem>(Priority::LogicStage::L02_PostUpdate);

    // ゲーム本編専用：ポーズ（ESC/START）＋ポーズメニュー。
    // 描画ステージに置き、ポーズ中も動いて解除入力を拾えるようにする。
    sysManager.RegisterSystem<PauseSystem>(Priority::RenderStage::R10_UI);
}

void EditorScene::FixedUpdate(float dt) 
{
    // B. 通常プレイ中
    // 親クラスの処理を呼んで、通常通り世界を動かす
    BaseScene::FixedUpdate(dt);
}

// Updateのオーバーライド
void EditorScene::Update(float dt)
{
    BaseScene::Update(dt);
    
}

void EditorScene::Render()
{
    BaseScene::Render();
}



// SceneFactory への自動登録（SceneManager が本クラスを include せずに済むようにする）
REGISTER_SCENE(EditorScene, "Editor")
