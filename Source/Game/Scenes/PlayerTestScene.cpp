#include "PlayerTestScene.h"
#include "Engine/Scene/SceneFactory.h"
#include "tracy/Tracy.hpp"

#include "Engine/Serialization/Factory/Prefab.h"
#include "Engine/Serialization/Serializer/LevelSerializer.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include "Engine/Platform/Core/Logger.h"

#include "Engine/ECS/System/CCL_SystemRegistry.h"

#include "Game/UI/Screens/PauseSystem.h"

void PlayerTestScene::Initialize()
{
    ZoneScopedN("PlayerTestScene::Initialize");

    BaseScene::Initialize(); // リソースの登録と RegisterSystems

    {
        ZoneScopedN("PlayerTestScene: LoadLevel PlayerTestStage");

        // BossBattleStage からボスだけを外したレベル。ドローンはボスが出すので、これで敵は 0 体になる
        const std::string playerTestLevel = "Assets/Scene/TestScene/PlayerTestStage.level.json";
        if (!LevelSerializer::LoadLevel(m_worldPtr, playerTestLevel))
            CCL_LOG_ERROR(LogCategory::Core, "PlayerTestScene: レベルの読み込みに失敗: %s", playerTestLevel.c_str());
    }

    // ムービー用カメラは、Cinematic Editor のプレビューを出すためだけに置く
    // 再生を頼まなければ止まったままなので、操作は止めない（CinematicCameraSystem の「4. 停止中」）
    {
        ZoneScopedN("PlayerTestScene: SpawnPrefab CinematicCamera");

        const std::string cinematicCameraPrefab = "Assets/Prefabs/Camera/CinematicCamera.json";
        if (Prefab::SpawnPrefab(*m_worldPtr, cinematicCameraPrefab) == CCL::ECS::InvalidEntityID)
        {
            // プレビューが出ないだけで確かめることはできるので、失敗しても止めない
            CCL_LOG_ERROR(LogCategory::Core,
                          "PlayerTestScene: ムービー用カメラの生成に失敗: %s", cinematicCameraPrefab.c_str());
        }
    }

    // 導入ムービーは頼まない。ボスを映す台本なので、ボスのいないこのシーンでは崩れた絵になる

    CCL_LOG_INFO(LogCategory::Core, "PlayerTestScene Initialize");
}



void PlayerTestScene::Finalize()
{
    BaseScene::Finalize();
}



void PlayerTestScene::RegisterSystems()
{
    auto &sysManager = *(m_ecsContext->GetSystemManager());
    CCL::ECS::SystemRegistry::Instance().RegisterAll(sysManager);

    // リザルト（GameResultSystem）は登録しない。理由はヘッダの備考を参照
    // GameFlowState は PauseSystem の Initialize が作るので、リザルトがなくても他の System は読める

    // ポーズは描画ステージに置く。ロジックステージはポーズ中に止まり、解除の入力を拾えなくなる
    sysManager.RegisterSystem<PauseSystem>(Priority::RenderStage::R10_UI);
}



REGISTER_SCENE(PlayerTestScene, "PlayerTest")
