/// @file GameScene.cpp
/// 役割・設計方針は GameScene.h を参照。
#include "GameScene.h"
#include "Engine/Scene/SceneFactory.h" // REGISTER_SCENE（ファイル末尾で自己登録）

#include "Engine/Serialization/Serializer/LevelSerializer.h" // レベル読み込み
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include "Engine/Platform/Core/Logger.h"

#include "Game/Logics/AI/Battle/BattleContext.h" // 敵AIが参照する戦況共有リソース

#include "Game/UI/GameResultSystem.h" // ゲームオーバー/クリア検知＋リザルトUI
#include "Game/UI/PauseSystem.h"      // ESC/STARTでポーズ＋ポーズメニュー

#include "tracy/Tracy.hpp"



void GameScene::Initialize()
{
    ZoneScopedN("GameScene::Initialize");

    BaseScene::Initialize(); // 共通初期化（リソース登録＋RegisterSystems）

    // 敵AIが受け取るシーン全体の情報が入った共有リソース。
    m_worldPtr->AddResource<BattleContext>();

    // --- 戦場レベルを読み込む ---
    // LoadLevel は内部で LoadScene（既存ワールドをPersistentTag以外全消去）→ ステージ復元
    // → プレハブ実体(ライト/カメラ/プレイヤー/ボス/ステージ)を再生成する。BaseScene::Initialize は
    // ゲーム実体を生成しないので、ここで全消去されて困るものは無い（リソース/システムは残る）。

    {
        ZoneScopedN("GameScene: LoadLevel BossBattleStage");

        // 起動時に読み込む戦場レベル。ライト/カメラ/プレイヤー/ボス/ステージを全て含む。
        std::string bossBattleLevel = "Assets/Scene/BossBattleStage.level.json";


        if (!LevelSerializer::LoadLevel(m_worldPtr, bossBattleLevel))
            CCL_LOG_ERROR(LogCategory::Core, "GameScene: 戦場レベルの読み込みに失敗: %s", bossBattleLevel.c_str());
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

// SceneFactory への自動登録（SceneManager が本クラスを include せずに済むようにする）
REGISTER_SCENE(GameScene, "Game")
