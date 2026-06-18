#include "EditorScene.h"
#include "Engine/Serialization/Factory/SceneInitializer.h"
#include "Engine/Graphics/RHI/Graphics.h"

#include "Engine/Serialization/Factory/Prefab.h"

#include "Engine/Platform/Core/Logger.h"

#include "Engine/ECS/System/CCL_SystemRegistry.h"

#include "Editor/Core/EditorCommandHistory.h"
#include "Engine/Transform/Components/TransformComponent.h"

#include "Engine/Transform/Systems/TransformUpdateSystem.h"

#include "Game/Logics/LevelGen/DungeonLevelBuilder.h"
#include "Game/Logics/LevelGen/DungeonLayout.h"

void EditorScene::Initialize()
{
    BaseScene::Initialize(); // 共通初期化


    ID3D11Device *device = Graphics::Instance().GetDevice();
    SceneInitializer::SpawnLightStartSet(*_worldPtr, device);
    //SceneInitializer::SpawnGameLightSetting(_world, device);
    //Prefab::SpawnPrefab(*_worldPtr, "Assets/Prefabs/Camera/FreeCamera.json");

    //SceneManager::Instance().LoadScene(_worldPtr, "Assets/Scene/DroneEnemyTestScene.json");

    //SceneInitializer::SpawnTransformLoadTest(*_worldPtr, device, 100000);
    //SceneInitializer::SpawnJoltTest(*_worldPtr, device);

    // 中では普通にPBRモデル用にしている
    //SceneInitializer::SpawnAnimationTest(*_worldPtr, device);

    //SceneInitializer::SpawnEnemySwarmTest(*_worldPtr, device,1000);
    //SceneInitializer::SpawnEnemySwarmTest(*_worldPtr, device,0);

    //SceneInitializer::SpawnDungeonTest(*_worldPtr, device, 2024, 1); // seed=2024, 第1層


    DungeonLevelBuilder::BuildParams p;
    p.seed = 2024;
    p.spawnPrefabs = true;
    //p.playerPrefab = "Assets/Prefabs/Player/PlayerTest.json";
    p.enemyPrefab = "Assets/Prefabs/Enemy/EnemyTest.json";
    //p.portalPrefab = "Assets/Prefabs/Portal/ExitPortal.json";
    // パスを空文字にすればその種類は配置しない
    //DungeonLevelBuilder::Build(*_worldPtr, device, p);


    // 手作りマップ（テスト用固定マップ）
    RuneForge::Dungeon::DungeonLayout layout;
    layout.LoadFromJson("Assets/DebugMaps/test1.json");
    DungeonLevelBuilder::BuildFromResult(*_worldPtr, device, layout.ToResult(), p);

    // プレイヤーをスタート地点に生成（カメラもセット）
    DungeonLevelBuilder::SpawnPlayerAtStart(*_worldPtr);

    //SceneInitializer::SpawnModelGridTest(*_worldPtr, device, 500);
    

    // 例：10万個のキューブによる銀河の渦を生成
    //SceneInitializer::SpawnECSBenchmarkTest(*_worldPtr, device, 100000);

    // 300 x 300 = 90,000 個のキューブの海を生成
    //SceneInitializer::SpawnVoxelOceanTest(*_worldPtr, device, 400);
    //SceneInitializer::SpawnVoxelOceanTest(*_worldPtr, device, 100);
   
    //SceneInitializer::SpawnPhysicsAvalancheTest(*_worldPtr, device, 10000);

    // 1000体の群衆ストレステストを実行！
    //SceneInitializer::SpawnEnemySwarmTest(*_worldPtr, device, 200);

	//SceneInitializer::SpawnTornadoTest(*_worldPtr, device, 1000);
	//SceneInitializer::SpawnVectorFlowTest(*_worldPtr, device, 1000);
	//SceneInitializer::SpawnLissajousTest(*_worldPtr, device, 10000);


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



