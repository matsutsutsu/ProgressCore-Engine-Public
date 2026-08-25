/// @file TitleScene.cpp
/// 役割・設計方針は TitleScene.h を参照。
#include "TitleScene.h"
#include "Engine/Scene/SceneFactory.h" // REGISTER_SCENE（ファイル末尾で自己登録）

#include "Engine/ECS/System/CCL_SystemManager.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

#include "Game/UI/TitleMenuSystem.h" // タイトル専用のメニュー制御

void TitleScene::RegisterSystems()
{
    auto &m = *(m_ecsContext->GetSystemManager());

    // 通常シーンと同じく全システムを登録する（タイトルでも3D描画/アニメ/パーティクル等を使うため）。
    // 戦闘/AI系はタイトルに登録しても安全:
    //   BattleContext は BattleContextSyncSystem が必ず登録するので存在するが、
    //   プレイヤー/ボス/ドローンの実体が無いため、各システムの ForEach が空回りして
    //   実質 no-op になる（HasPlayer()/HasBoss() も false のまま）。
    CCL::ECS::SystemRegistry::Instance().RegisterAll(m);

    // タイトル専用：メニュー生成＋クリックで遷移（グローバル登録しないのでここで手動登録）
    m.RegisterSystem<TitleMenuSystem>(Priority::LogicStage::L02_Update);
}

// SceneFactory への自動登録（SceneManager が本クラスを include せずに済むようにする）
REGISTER_SCENE(TitleScene, "Title")
