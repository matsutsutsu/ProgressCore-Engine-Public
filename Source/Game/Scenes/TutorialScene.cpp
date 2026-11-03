#include "TutorialScene.h"
#include "Engine/Scene/SceneFactory.h"

#include "Engine/Serialization/Serializer/LevelSerializer.h" // レベル読み込み
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include "Engine/Platform/Core/Logger.h"

#include "Game/Logics/Tutorial/TutorialSystem.h"            // 教える項目の進行
#include "Game/Logics/Tutorial/TutorialHealthGuardSystem.h" // プレイヤーを倒れさせず、ボスの形態を移らせない
#include "Game/UI/HUD/TutorialHintHudSystem.h"              // ヒントの表示
#include "Game/UI/Input/InputPromptSystem.h"                // 右上の操作の説明
#include "Game/UI/Screens/TutorialCompleteSystem.h"         // 教え終えた後の画面
#include "Game/UI/Screens/PauseSystem.h"                    // ESC/STARTでポーズ＋ポーズメニュー

#include "tracy/Tracy.hpp"

void TutorialScene::Initialize()
{
    ZoneScopedN("TutorialScene::Initialize");

    BaseScene::Initialize(); // リソースの登録と RegisterSystems

    // LoadLevel は PersistentTag の付いていないエンティティを全部消してから読み込む
    {
        ZoneScopedN("TutorialScene: LoadLevel BossBattleStage");

        // 本番と同じ戦場を使う。チュートリアルだけ別の場所にすると、本番に入ったときに雰囲気が切り替わってしまう
        const std::string bossBattleLevel = "Assets/Scene/BossBattleStage.level.json";
        if (!LevelSerializer::LoadLevel(m_worldPtr, bossBattleLevel))
            CCL_LOG_ERROR(LogCategory::Core, "TutorialScene: 戦場レベルの読み込みに失敗: %s", bossBattleLevel.c_str());
    }

    // 導入ムービーは頼まない。レベルに入っている TPS のカメラで、そのまま操作から始める

    CCL_LOG_INFO(LogCategory::Core, "TutorialScene Initialize");
}

void TutorialScene::Finalize()
{
    BaseScene::Finalize();
}

void TutorialScene::RegisterSystems()
{
    auto &sysManager = *(m_ecsContext->GetSystemManager());
    CCL::ECS::SystemRegistry::Instance().RegisterAll(sysManager);

    // --- チュートリアルだけで使う System（全体登録していないのでここで登録する） ---

    // ヒントを出す TutorialHintHudSystem と同じステージに置き、同じフレームのうちに映させる。
    // TutorialState のリソースはこの System の Initialize が作る
    sysManager.RegisterSystem<TutorialSystem>(Priority::LogicStage::L06_HudSync);

    // 他の HUD と同じステージ。TutorialSystem の後に走る（GetRunAfter）
    sysManager.RegisterSystem<TutorialHintHudSystem>(Priority::LogicStage::L06_HudSync);

    // 操作の説明。チュートリアルの間だけ右上に出しておく。ゲージと同じ HUD のステージに置く
    sysManager.RegisterSystem<InputPromptSystem>(Priority::LogicStage::L06_HudSync);

    // ダメージで HP を引いた（L06_DamageApply）後、0 以下で倒れたことにする（L06_HealthManagement）前に HP を戻す
    sysManager.RegisterSystem<TutorialHealthGuardSystem>(Priority::LogicStage::L06_StatusEffect);

    // 完了の画面。本番のリザルト（GameResultSystem）と同じステージに置く
    sysManager.RegisterSystem<TutorialCompleteSystem>(Priority::LogicStage::L02_PostUpdate);

    // ポーズは描画ステージに置く。ロジックステージはポーズ中に止まり、解除の入力を拾えなくなる
    sysManager.RegisterSystem<PauseSystem>(Priority::RenderStage::R10_UI);
}

REGISTER_SCENE(TutorialScene, "Tutorial")
