// 役割・設計方針は TitleScene.h を参照。
#include "TitleScene.h"
#include "Engine/Scene/SceneFactory.h" // REGISTER_SCENE（ファイル末尾で自己登録）

#include "Engine/ECS/System/CCL_SystemManager.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include "Engine/Platform/Core/Logger.h"
#include "Engine/Serialization/Serializer/LevelSerializer.h" // タイトル背景レベルの読み込み

#include "Game/Title/TitleCameraSystem.h" // 構図のカメラ演出
#include "Game/Title/TitleSwarmSystem.h"  // 群れが斬線を組む振り付け
#include "Game/UI/TitleMenuSystem.h"      // タイトル専用のメニュー制御

void TitleScene::Initialize()
{
    BaseScene::Initialize(); // 共通初期化＋RegisterSystems

    // 3D背景。ここで読むのが要点で、レベル側にメニューUIを持たせることはできない
    // （LoadLevel は PersistentTag 以外を全消去するため、UIは読み込み後に生成される）。
    const std::string titleLevel = "Assets/Scene/Title.level.json";
    if (!LevelSerializer::LoadLevel(m_worldPtr, titleLevel))
    {
        // 背景が出ないだけでメニューは機能する（黒背景のタイトルになる）ので止めない。
        CCL_LOG_ERROR(LogCategory::Core, "TitleScene: タイトル背景の読み込みに失敗: %s",
                      titleLevel.c_str());
    }

    CCL_LOG_INFO(LogCategory::Core, "TitleScene Initialize");
}

void TitleScene::RegisterSystems()
{
    auto &m = *(m_ecsContext->GetSystemManager());

    // 通常シーンと同じく全システムを登録する（タイトルでも3D描画/アニメ/パーティクル、
    // それにドローンの移動・判断層をそのまま使うため）。
    // 戦闘/AI系はタイトルに登録しても安全:
    //   BattleContext は BattleContextSyncSystem が必ず登録するので存在するが、
    //   プレイヤー/ボスの実体が無いため、各システムの ForEach が空回りして
    //   実質 no-op になる（HasPlayer()/HasBoss() も false のまま）。
    //   ドローンへの命令も DroneCommandSystem がボス不在で即 return する＝
    //   タイトルの群れは TitleSwarmSystem の振り付けだけに従う。
    CCL::ECS::SystemRegistry::Instance().RegisterAll(m);

    // --- タイトル専用（グローバル登録しないのでここで手動登録）---

    // 群れの振り付け。移動層(L03_PrePhysics)より前に持ち場を書く必要がある。
    m.RegisterSystem<TitleSwarmSystem>(Priority::LogicStage::L02_PostUpdate);

    // カメラ。他のカメラボディと同じステージに置き、Brain(R01b)より前に姿勢を確定させる。
    m.RegisterSystem<TitleCameraSystem>(Priority::RenderStage::R01_Camera);

    // メニュー生成＋クリックで遷移
    m.RegisterSystem<TitleMenuSystem>(Priority::LogicStage::L02_Update);
}

// SceneFactory への自動登録（SceneManager が本クラスを include せずに済むようにする）
REGISTER_SCENE(TitleScene, "Title")
