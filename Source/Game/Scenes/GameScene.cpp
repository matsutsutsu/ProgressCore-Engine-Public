#include "GameScene.h"
#include "Engine/Scene/SceneFactory.h"

#include "Engine/Serialization/Serializer/LevelSerializer.h" // レベル読み込み
#include "Engine/Serialization/Factory/Prefab.h"             // 導入カメラ演出の生成
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include "Engine/Platform/Core/Logger.h"

#include "Game/UI/Screens/GameResultSystem.h" // ゲームオーバー/クリア検知＋リザルトUI
#include "Game/UI/Screens/PauseSystem.h"      // ESC/STARTでポーズ＋ポーズメニュー

#include "tracy/Tracy.hpp"

void GameScene::Initialize()
{
    ZoneScopedN("GameScene::Initialize");

    BaseScene::Initialize(); // リソースの登録と RegisterSystems

    // LoadLevel は PersistentTag の付いていないエンティティを全部消してから読み込む
    {
        ZoneScopedN("GameScene: LoadLevel BossBattleStage");

        std::string bossBattleLevel = "Assets/Scene/BossBattleStage.level.json";

        if (!LevelSerializer::LoadLevel(m_worldPtr, bossBattleLevel))
            CCL_LOG_ERROR(LogCategory::Core, "GameScene: 戦場レベルの読み込みに失敗: %s", bossBattleLevel.c_str());
    }

    // 導入ムービーはレベルに入れずここで足す。レベルは EditorScene も読むので、
    // レベルに入れるとデバッグで起動するたびに数秒の演出を待つことになる
    {
        ZoneScopedN("GameScene: SpawnPrefab BossIntroTimeline");

        const std::string introCameraPrefab = "Assets/Prefabs/Camera/BossIntroTimeline.json";
        if (Prefab::SpawnPrefab(*m_worldPtr, introCameraPrefab) == CCL::ECS::InvalidEntityID)
        {
            // 演出が出ないだけでゲームは成立するので、失敗しても止めない。
            CCL_LOG_ERROR(LogCategory::Core,
                          "GameScene: 導入ムービーの生成に失敗: %s", introCameraPrefab.c_str());
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

    // リザルトとポーズはゲーム本編でだけ動かしたいので、全体登録（REGISTER_*）せずここで登録する
    sysManager.RegisterSystem<GameResultSystem>(Priority::LogicStage::L02_PostUpdate);

    // ポーズは描画ステージに置く。ロジックステージはポーズ中に止まり、解除の入力を拾えなくなる
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

REGISTER_SCENE(GameScene, "Game")
