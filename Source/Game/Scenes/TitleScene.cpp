#include "TitleScene.h"
#include "Engine/Scene/SceneFactory.h"

#include "Engine/ECS/System/CCL_SystemManager.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include "Engine/Platform/Core/Logger.h"
#include "Engine/Serialization/Serializer/LevelSerializer.h"

#include "Game/Logics/Title/TitleCameraSystem.h"
#include "Game/Logics/Title/TitleSwarmSystem.h"
#include "Game/UI/Screens/TitleMenuSystem.h"
#include "Game/Logics/Tutorial/TutorialState.h" // タイトルに入るたびに「教えた」記録を消す

void TitleScene::Initialize()
{
    BaseScene::Initialize(); // リソースの登録と RegisterSystems

    // タイトルを通った挑戦だけチュートリアルを出す（エディタシーンから直接遊ぶときは出さない）。
    // タイトルへ戻ったら、次に遊ぶ人へまた最初から教える（展示会では遊び手がここで入れ替わる）。
    // リトライはタイトルを通らないので、教え終えた人には繰り返さない
    TutorialSession::Instance().armed     = true;
    TutorialSession::Instance().completed = false;

    // メニューはレベルに入れられない。LoadLevel は PersistentTag の付いていないエンティティを
    // 全部消すので、メニューは読み込みの後で TitleMenuSystem が作る
    const std::string titleLevel = "Assets/Scene/Title.level.json";
    if (!LevelSerializer::LoadLevel(m_worldPtr, titleLevel))
    {
        // 背景が出ないだけでメニューは使える（黒い背景になる）ので止めない
        CCL_LOG_ERROR(LogCategory::Core, "TitleScene: タイトル背景の読み込みに失敗: %s",
                      titleLevel.c_str());
    }

    CCL_LOG_INFO(LogCategory::Core, "TitleScene Initialize");
}

void TitleScene::RegisterSystems()
{
    auto &m = *(m_ecsContext->GetSystemManager());

    // 他のシーンと同じく全 System を登録する。タイトルでも 3D 描画・アニメーション・パーティクルと、
    // ドローンの移動を使うため。
    // 戦闘と AI の System を登録しても問題はない。BattleContext は BattleContextSyncSystem が
    // 必ず登録するが、プレイヤーとボスが居ないので各 System の ForEach は何もしない
    // （HasPlayer() / HasBoss() も false のまま）。DroneCommandSystem もボスが居ないとすぐ戻るので、
    // タイトルの群れは TitleSwarmSystem の指示だけで動く
    CCL::ECS::SystemRegistry::Instance().RegisterAll(m);

    // --- タイトルだけで使う System（全体登録していないのでここで登録する） ---

    // 群れの配置。移動（L03_PrePhysics）より前に持ち場を書く必要がある
    m.RegisterSystem<TitleSwarmSystem>(Priority::LogicStage::L02_PostUpdate);

    // カメラ。他のカメラと同じステージに置き、CameraBrainSystem（R01b）より前に姿勢を決める
    m.RegisterSystem<TitleCameraSystem>(Priority::RenderStage::R01_Camera);

    // メニューの生成と、ボタンでのシーン遷移
    m.RegisterSystem<TitleMenuSystem>(Priority::LogicStage::L02_Update);
}

// SceneFactory へ登録する（SceneManager がこのクラスを include せずに済むように）
REGISTER_SCENE(TitleScene, "Title")
