#include "EditorScene.h"
#include "Engine/Serialization/Factory/SceneInitializer.h"
#include "Engine/Graphics/RHI/Graphics.h"

#include "Engine/Serialization/Factory/Prefab.h"
#include "Engine/Platform/Core/Logger.h"

#include "Engine/ECS/System/CCL_SystemRegistry.h"

#include "Game/Logics/AI/StateTree/Core/StateTreeRegistry.h"

#include "Game/Logics/AI/StateTree/Commander/CommanderAI.h"
#include "Game/Logics/AI/BehaviorTree/Actions/BTActions_Boss.h"
#include "Game/Logics/AI/BehaviorTree/Actions/BTConditions.h"


void EditorScene::Initialize()
{
    BaseScene::Initialize(); // 共通初期化


    ID3D11Device *device = Graphics::Instance().GetDevice();
    SceneInitializer::SpawnLightStartSet(*_worldPtr, device);
    //Prefab::SpawnPrefab(*_worldPtr, "Assets/Prefabs/Camera/FreeCamera.json");

    SceneInitializer::SpawnGameTest(*_worldPtr, device);

    //SceneManager::Instance().LoadScene(_worldPtr,"Assets/Scene/PlayerMovementScene.json");

    //static auto enemyIds = EnemyAI::RegisterNodes();

    // ★追加：BTのアクション・条件を登録
    CCL::AI::BT::BossActions::RegisterAll();
    CCL::AI::BT::BossConditions::RegisterAll();

    // ★追加：司令官のノードを登録
    static auto commanderIds = CommanderAI::RegisterNodes();

    _worldPtr->AddResource<CCL::AI::StateTreeRegistry>();
    auto &stReg       = _worldPtr->GetResource<CCL::AI::StateTreeRegistry>();
    //auto  enemyHandle = stReg.Register(EnemyAI::BuildEnemyAsset(enemyIds, 12.0f));

    // ★追加：司令官StateTreeアセットを登録
    stReg.Register(CommanderAI::BuildCommanderAsset(commanderIds));

    // 敵AIが受け取るシーン全体の情報が入った共有のリソースを登録
    _worldPtr->AddResource<BattleContext>();


    CCL_LOG_INFO(LogCategory::Core, "EditorScene Initialize");
}

void EditorScene::Finalize() 
{
    BaseScene::Finalize(); // 共通終了化
}

void EditorScene::RegisterSystems()
{
    // SystemManagerへのアクセスを _ecsContext 経由に変更
    // (SystemGroupsの関数が参照を受け取る仕様であると推測されるため、ポインタをデリファレンスしています)
    auto &sysManager = *(_ecsContext->GetSystemManager());
    CCL::ECS::SystemRegistry::Instance().RegisterAll(sysManager);

}

void EditorScene::FixedUpdate(float dt) 
{
    // B. 通常プレイ中
    // 親クラスの処理を呼んで、通常通り世界を動かす
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



