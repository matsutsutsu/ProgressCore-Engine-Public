#include "EditorScene.h"
#include "Engine/Serialization/Factory/SceneInitializer.h"
#include "Engine/Graphics/Core/Graphics.h"
#include <imgui.h>

#include "Engine/Serialization/Factory/Prefab.h"

#include "Engine/Platform/Logger.h"

#include "ECS/System/CCL_SystemRegistry.h"

#include "Editor/Utils/NavMeshBuilder.h"

#include <ImGuizmo.h>
#include "Editor/Core/EditorCommandHistory.h"
#include "Engine/GamePlay/Transform/TransformComponent.h"

#include "Engine/GamePlay/Transform/TransformUpdateSystem.h"

void EditorScene::Initialize()
{
    BaseScene::Initialize(); // 共通初期化


    ID3D11Device *device = Graphics::Instance().GetDevice();
    SceneInitializer::SpawnLightStartSet(*_worldPtr, device);
    //SceneInitializer::SpawnGameLightSetting(_world, device);
    //Prefab::SpawnPrefab(*_worldPtr, "Assets/Prefabs/Camera/FreeCamera.json");
    //Prefab::SpawnPrefab(*_worldPtr, "Assets/Prefabs/Camera/FreeFastCamera.json");

    //SceneManager::Instance().LoadScene(_worldPtr, "Assets/Scene/DroneEnemyTestScene.json");

    //SceneInitializer::SpawnTransformLoadTest(*_worldPtr, device, 100000);
    //SceneInitializer::SpawnJoltTest(*_worldPtr, device);

    // 中では普通にPBRモデル用にしている
    SceneInitializer::SpawnAnimationTest(*_worldPtr, device);

    //SceneInitializer::SpawnBossAndDronesTest(*_worldPtr, device);


    // ボスとドローン群のテスト環境を展開
    //SceneInitializer::SpawnBossAndDronesTest(*_worldPtr, device);


    //SceneInitializer::SpawnModelGridTest(*_worldPtr, device, 500);
    
    //SceneInitializer::SpawnPlayerCarTest(*_worldPtr, device);

    //SceneInitializer::SpawnShaderTest(*_worldPtr, device);

    // 例：10万個のキューブによる銀河の渦を生成
    //SceneInitializer::SpawnECSBenchmarkTest(*_worldPtr, device, 100000);

    // 300 x 300 = 90,000 個のキューブの海を生成
    //SceneInitializer::SpawnVoxelOceanTest(*_worldPtr, device, 300);
   
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
    ID3D11Device *device = Graphics::Instance().GetDevice();

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



