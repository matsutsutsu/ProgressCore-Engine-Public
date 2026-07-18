/// @file TitleScene.cpp
/// 役割・設計方針は TitleScene.h を参照。
#include "TitleScene.h"

#include "Engine/ECS/System/CCL_SystemManager.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

#include "Game/UI/TitleMenuSystem.h" // タイトル専用のメニュー制御

void TitleScene::RegisterSystems()
{
    auto &m = *(_ecsContext->GetSystemManager());

    // 通常シーンと同じく全システムを登録する（タイトルでも3D描画/アニメ/パーティクル等を使うため）。
    // 戦闘/AI系は必要リソース(BattleContext等)やエンティティが無い場合、各システム側が
    // HasResource ガードで自動的に no-op になるため、タイトルに登録しても安全。
    CCL::ECS::SystemRegistry::Instance().RegisterAll(m);

    // タイトル専用：メニュー生成＋クリックで遷移（グローバル登録しないのでここで手動登録）
    m.RegisterSystem<TitleMenuSystem>(Priority::LogicStage::L02_Update);
}
