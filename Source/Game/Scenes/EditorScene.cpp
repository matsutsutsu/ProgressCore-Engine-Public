#include "EditorScene.h"
#include "Engine/Scene/SceneFactory.h"
#include "Engine/Cinematic/CinematicEvents.h" // 導入ムービーの再生要求
#include "tracy/Tracy.hpp"

#include "Engine/Serialization/Factory/Prefab.h"
#include "Engine/Serialization/Serializer/LevelSerializer.h" // 戦場レベルの読み込み
#include "Engine/Platform/Core/Logger.h"

#include "Engine/ECS/System/CCL_SystemRegistry.h"

#include "Game/UI/Screens/GameResultSystem.h" // ゲームオーバー検知＋リザルトUI
#include "Game/UI/Screens/PauseSystem.h"      // ESC/STARTでポーズ＋ポーズメニュー

namespace
{
    constexpr const char *kIntroCinematic = "Assets/Cinematics/BossIntro.json"; // 導入ムービーの台本
}

void EditorScene::Initialize()
{
    ZoneScopedN("EditorScene::Initialize");
    
    BaseScene::Initialize();

    {
        ZoneScopedN("EditorScene: LoadLevel BossBattleStage");

        const std::string bossBattleLevel = "Assets/Scene/BossBattleStage.level.json";
        if (!LevelSerializer::LoadLevel(m_worldPtr, bossBattleLevel))
            CCL_LOG_ERROR(LogCategory::Core, "EditorScene: 戦場レベルの読み込みに失敗: %s", bossBattleLevel.c_str());
    }

    // ムービー用カメラは GameScene と同じくここで足す。
    // エディタでもムービーを流し、Cinematic Editor のプレビューを出すため。
    {
        ZoneScopedN("EditorScene: SpawnPrefab CinematicCamera");

        const std::string cinematicCameraPrefab = "Assets/Prefabs/Camera/CinematicCamera.json";
        if (Prefab::SpawnPrefab(*m_worldPtr, cinematicCameraPrefab) == CCL::ECS::InvalidEntityID)
        {
            // ムービーが出ないだけで開発は続けられるので、失敗しても止めない。
            CCL_LOG_ERROR(LogCategory::Core,
                          "EditorScene: ムービー用カメラの生成に失敗: %s", cinematicCameraPrefab.c_str());
        }
    }

    // 導入ムービーは GameScene と同じく、シーンの準備として頼んでおく（理由は GameScene::Initialize）。
    // 開発中に待ちたくなければ、長押しでスキップできる。
    m_worldPtr->GetEventBus().Publish(CinematicPlayRequestEvent{kIntroCinematic});

    CCL_LOG_INFO(LogCategory::Core, "EditorScene Initialize");
}

void EditorScene::Finalize()
{
    BaseScene::Finalize();
}

void EditorScene::RegisterSystems()
{
    auto &sysManager = *(m_ecsContext->GetSystemManager());
    CCL::ECS::SystemRegistry::Instance().RegisterAll(sysManager);

    // リザルトとポーズはゲーム本編でだけ動かしたいので、全体登録（REGISTER_*）せずここで登録する。
    // タイトルには入らない
    sysManager.RegisterSystem<GameResultSystem>(Priority::LogicStage::L02_PostUpdate);

    // ポーズは描画ステージに置く。ロジックステージはポーズ中に止まり、解除の入力を拾えなくなる
    sysManager.RegisterSystem<PauseSystem>(Priority::RenderStage::R10_UI);
}

void EditorScene::FixedUpdate(float dt)
{
    BaseScene::FixedUpdate(dt);
}

void EditorScene::Update(float dt)
{
    BaseScene::Update(dt);
}

void EditorScene::Render()
{
    BaseScene::Render();
}

REGISTER_SCENE(EditorScene, "Editor")
