#include "GameScene.h"
#include "Engine/Scene/SceneFactory.h"
#include "Engine/Cinematic/CinematicEvents.h"

#include "Engine/Serialization/Serializer/LevelSerializer.h"
#include "Engine/Serialization/Factory/Prefab.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include "Engine/Platform/Core/Logger.h"

#include "Game/UI/Screens/GameResultSystem.h"
#include "Game/UI/Screens/PauseSystem.h"

#include "tracy/Tracy.hpp"

namespace
{
    constexpr const char *kIntroCinematic = "Assets/Cinematics/BossIntro.json"; // 導入ムービーの台本
}

void GameScene::Initialize()
{
    ZoneScopedN("GameScene::Initialize");

    BaseScene::Initialize(); // リソースの登録と RegisterSystems
  
    {
        ZoneScopedN("GameScene: LoadLevel BossBattleStage");

        std::string bossBattleLevel = "Assets/Scene/BossBattleStage.level.json";

        if (!LevelSerializer::LoadLevel(m_worldPtr, bossBattleLevel))
            CCL_LOG_ERROR(LogCategory::Core, "GameScene: 戦場シーンの読み込みに失敗: %s", bossBattleLevel.c_str());
    }

    // ムービー用カメラはレベルに入れずここで足す
    // ムービーがあるかどうかはレベル側ではなくシーン側が持つ
    {
        ZoneScopedN("GameScene: SpawnPrefab CinematicCamera");

        const std::string cinematicCameraPrefab = "Assets/Prefabs/Camera/CinematicCamera.json";
        if (Prefab::SpawnPrefab(*m_worldPtr, cinematicCameraPrefab) == CCL::ECS::InvalidEntityID)
        {
            // ムービーが出ないだけでゲームは成立するので、失敗しても止めない
            CCL_LOG_ERROR(LogCategory::Core,
                          "GameScene: ムービー用カメラの生成に失敗: %s", cinematicCameraPrefab.c_str());
        }
    }

    // 導入ムービーのイベントを送る
    m_worldPtr->GetEventBus().Publish(CinematicPlayRequestEvent{kIntroCinematic});

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

    // リザルトとポーズはゲーム本編でだけ動かしたいので、ここで個別に登録する
    sysManager.RegisterSystem<GameResultSystem>(Priority::LogicStage::L02_PostUpdate);

    // ゲームのポーズ処理を登録する。ゲーム専用にしたいので
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
