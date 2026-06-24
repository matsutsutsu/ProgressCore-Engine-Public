#include "EditorScene.h"
#include "Engine/Serialization/Factory/SceneInitializer.h"
#include "Engine/Graphics/RHI/Graphics.h"

#include "Engine/Serialization/Factory/Prefab.h"
#include "Engine/Platform/Core/Logger.h"

#include "Engine/ECS/System/CCL_SystemRegistry.h"


#include "Game/Logics/AI/StateTree/EnemyAI/EnemyAI.h"
#include "Game/Logics/AI/StateTree/Core/StateTreeRegistry.h"


void EditorScene::Initialize()
{
    BaseScene::Initialize(); // 共通初期化


    ID3D11Device *device = Graphics::Instance().GetDevice();
    SceneInitializer::SpawnLightStartSet(*_worldPtr, device);
    //Prefab::SpawnPrefab(*_worldPtr, "Assets/Prefabs/Camera/FreeCamera.json");

    SceneInitializer::SpawnAnimationTest(*_worldPtr, device);

    //SceneManager::Instance().LoadScene(_worldPtr,"Assets/Scene/PlayerMovementScene.json");

    // (a) ノード種別を登録
    static auto enemyIds = EnemyAI::RegisterNodes();   // アプリ起動中1回でOK
  
    _worldPtr->AddResource<CCL::AI::StateTreeRegistry>();          // 空で生成
    auto& stReg = _worldPtr->GetResource<CCL::AI::StateTreeRegistry>();
    auto enemyHandle = stReg.Register(EnemyAI::BuildEnemyAsset(enemyIds, 12.0f));
   


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



