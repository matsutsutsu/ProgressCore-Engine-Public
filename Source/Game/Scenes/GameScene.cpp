#include "GameScene.h"
#include "Engine/Scene/SceneFactory.h"
#include "Engine/Cinematic/CinematicEvents.h"   // 導入ムービーの再生要求

#include "Engine/Serialization/Serializer/LevelSerializer.h" // レベル読み込み
#include "Engine/Serialization/Factory/Prefab.h"             // 導入カメラ演出の生成
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include "Engine/Platform/Core/Logger.h"

#include "Game/UI/Screens/GameResultSystem.h" // ゲームオーバー/クリア検知＋リザルトUI
#include "Game/UI/Screens/PauseSystem.h"      // ESC/STARTでポーズ＋ポーズメニュー

#include "tracy/Tracy.hpp"

namespace
{
    constexpr const char *kIntroCinematic = "Assets/Cinematics/BossIntro.json"; // 導入ムービーの台本
}

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

    // ムービー用カメラはレベルに入れずここで足す（EditorScene も同じく自分で足す）。
    // ムービーを使うかどうかを、レベルのデータではなくシーンのコードで決めるため
    {
        ZoneScopedN("GameScene: SpawnPrefab CinematicCamera");

        const std::string cinematicCameraPrefab = "Assets/Prefabs/Camera/CinematicCamera.json";
        if (Prefab::SpawnPrefab(*m_worldPtr, cinematicCameraPrefab) == CCL::ECS::InvalidEntityID)
        {
            // ムービーが出ないだけでゲームは成立するので、失敗しても止めない。
            CCL_LOG_ERROR(LogCategory::Core,
                          "GameScene: ムービー用カメラの生成に失敗: %s", cinematicCameraPrefab.c_str());
        }
    }

    // 導入ムービーは、シーンの準備として頼んでおく（シーン遷移の明転を待たない）。
    // 明転の間もロジックは進むので、待つとその間にボスが動き出し、ムービーの構図が崩れる。
    // 受け手は要求を控えるだけで、取り出すのは最初のフレームの描画の更新なので、ロジックの1歩目から操作と AI が止まる。
    // ムービーの頭は明転と重なるので、台本の最初のショットは明転の長さを見込んで書く。
    // 非同期読み込みではこの関数は裏のスレッドで走るが、受け手は mutex で守って控えるだけなので問題ない。
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
