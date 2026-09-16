#include "EditorScene.h"
#include "Engine/Scene/SceneFactory.h"
#include "tracy/Tracy.hpp"

#include "Engine/Serialization/Factory/Prefab.h"
#include "Engine/Serialization/Serializer/LevelSerializer.h" // 戦場レベルの読み込み
#include "Engine/Platform/Core/Logger.h"

#include "Engine/ECS/System/CCL_SystemRegistry.h"

#include "Game/UI/GameResultSystem.h" // ゲームオーバー検知＋リザルトUI
#include "Game/UI/PauseSystem.h"      // ESC/STARTでポーズ＋ポーズメニュー

void EditorScene::Initialize()
{
    ZoneScopedN("EditorScene::Initialize");

    BaseScene::Initialize(); // 共通初期化

    // シーン読み込み
    {
        ZoneScopedN("EditorScene: LoadLevel BossBattleStage");

        const std::string bossBattleLevel = "Assets/Scene/BossBattleStage.level.json";
        if (!LevelSerializer::LoadLevel(m_worldPtr, bossBattleLevel))
            CCL_LOG_ERROR(LogCategory::Core, "EditorScene: 戦場レベルの読み込みに失敗: %s", bossBattleLevel.c_str());
    }

    CCL_LOG_INFO(LogCategory::Core, "EditorScene Initialize");
}

void EditorScene::Finalize()
{
    BaseScene::Finalize();
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

REGISTER_SCENE(EditorScene, "Editor")
