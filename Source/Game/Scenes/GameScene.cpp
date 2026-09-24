#include "GameScene.h"
#include "Engine/Scene/SceneFactory.h"

#include "Engine/Serialization/Serializer/LevelSerializer.h" // レベル読み込み
#include "Engine/Serialization/Factory/Prefab.h"             // 導入カメラ演出の生成
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include "Engine/Platform/Core/Logger.h"

#include "Game/UI/GameResultSystem.h" // ゲームオーバー/クリア検知＋リザルトUI
#include "Game/UI/PauseSystem.h"      // ESC/STARTでポーズ＋ポーズメニュー

#include "tracy/Tracy.hpp"

void GameScene::Initialize()
{
    ZoneScopedN("GameScene::Initialize");

    BaseScene::Initialize(); // 共通初期化（リソース登録＋RegisterSystems）

    // 戦場レベルを読み込む
    // LoadLevel は内部で LoadScene（既存ワールドをPersistentTag以外全消去）→ ステージ復元
    {
        ZoneScopedN("GameScene: LoadLevel BossBattleStage");

        // 起動時に読み込む戦場レベル。ライト/カメラ/プレイヤー/ボス/ステージを全て含む。
        std::string bossBattleLevel = "Assets/Scene/BossBattleStage.level.json";

        if (!LevelSerializer::LoadLevel(m_worldPtr, bossBattleLevel))
            CCL_LOG_ERROR(LogCategory::Core, "GameScene: 戦場レベルの読み込みに失敗: %s", bossBattleLevel.c_str());
    }

    // 導入カメラ演出
    // レベル(BossBattleStage.level.json)ではなく、ここで足すのが要点。
    //   レベルは EditorScene も読むため、レベル側に入れるとデバッグ起動のたびに
    //   数秒のムービーを待たされる。「本編でだけ再生する」ものは本編のシーンが持つ。
    {
        ZoneScopedN("GameScene: SpawnPrefab BossIntroCamera");

        const std::string introCameraPrefab = "Assets/Prefabs/Camera/BossIntroCamera.json";
        if (Prefab::SpawnPrefab(*m_worldPtr, introCameraPrefab) == CCL::ECS::InvalidEntityID)
        {
            // 演出が出ないだけでゲームは成立するので、失敗しても止めない。
            CCL_LOG_ERROR(LogCategory::Core,
                          "GameScene: 導入カメラ演出の生成に失敗: %s", introCameraPrefab.c_str());
        }
    }

    CCL_LOG_INFO(LogCategory::Core, "GameScene Initialize");
}

void GameScene::Finalize()
{
    BaseScene::Finalize();
}

void GameScene::RegisterSystems()
{
    auto &sysManager = *(m_ecsContext->GetSystemManager());
    CCL::ECS::SystemRegistry::Instance().RegisterAll(sysManager);

    // ゲーム本編専用：ゲームオーバー/クリア検知＋リザルトUIオーバーレイ。
    sysManager.RegisterSystem<GameResultSystem>(Priority::LogicStage::L02_PostUpdate);

    // ゲーム本編専用：ポーズ（ESC/START）＋ポーズメニュー。
    // 描画ステージに置き、ポーズ中も動いて解除入力を拾えるようにする。
    sysManager.RegisterSystem<PauseSystem>(Priority::RenderStage::R10_UI);
}

void GameScene::FixedUpdate(float dt)
{
    BaseScene::FixedUpdate(dt);
}

void GameScene::Update(float dt)
{
    BaseScene::Update(dt);
}

void GameScene::Render()
{
    BaseScene::Render();
}

// SceneFactory への自動登録
REGISTER_SCENE(GameScene, "Game")
