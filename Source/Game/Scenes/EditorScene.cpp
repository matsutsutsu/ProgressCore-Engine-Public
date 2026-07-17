#include "EditorScene.h"
#include "Engine/Serialization/Factory/SceneInitializer.h"
#include "Engine/Graphics/RHI/Graphics.h"
#include "tracy/Tracy.hpp"

#include "Engine/Serialization/Factory/Prefab.h"
#include "Engine/Platform/Core/Logger.h"

#include "Engine/ECS/System/CCL_SystemRegistry.h"

#include "Game/Logics/AI/StateTree/Core/StateTreeRegistry.h"
#include "Game/Logics/AI/StateTree/Serialization/StateTreeSerializer.h"

#include "Game/Logics/AI/StateTree/Commander/CommanderAI.h"

void EditorScene::Initialize()
{
    ZoneScopedN("EditorScene::Initialize");

    BaseScene::Initialize(); // 共通初期化


    ID3D11Device *device = Graphics::Instance().GetDevice();
    {
        ZoneScopedN("EditorScene: SpawnLightStartSet");
        SceneInitializer::SpawnLightStartSet(*_worldPtr, device);
    }
    //Prefab::SpawnPrefab(*_worldPtr, "Assets/Prefabs/Camera/FreeCamera.json");

    // ★ 起動コストの主因候補：テスト用エンティティ（ドローン多数など）の一括生成。
    {
        ZoneScopedN("EditorScene: SpawnGameTest");
        SceneInitializer::SpawnGameTest(*_worldPtr, device);
    }

    //SceneManager::Instance().LoadScene(_worldPtr,"Assets/Scene/PlayerMovementScene.json");

    //static auto enemyIds = EnemyAI::RegisterNodes();



    // 司令官の Task/Condition（語彙）を NodeRegistry へ登録
    // 木構造はここでは組まない。StateTreeWindow で編集した Assets/AI/Commander.json を読む。
    static auto commanderIds = CommanderAI::RegisterNodes();
    (void)commanderIds;

    _worldPtr->AddResource<CCL::AI::StateTreeRegistry>();
    auto &stReg       = _worldPtr->GetResource<CCL::AI::StateTreeRegistry>();
    //auto  enemyHandle = stReg.Register(EnemyAI::BuildEnemyAsset(enemyIds, 12.0f));

    // 司令官StateTreeアセットをJSONからロード（StateTreeWindowで組み立てたものがそのまま使われる）
    {
        ZoneScopedN("EditorScene: StateTree load+compile");
        CCL::AI::StateTreeDef commanderDef;
        if (CCL::AI::StateTreeSerializer::LoadDef("Assets/AI/Commander.json", commanderDef))
            stReg.Register(CCL::AI::StateTreeSerializer::Compile(commanderDef));
    }


    // 敵AIが受け取るシーン全体の情報が入った共有のリソースを登録
    _worldPtr->AddResource<BattleContext>();

    auto &battle           = _worldPtr->GetResource<BattleContext>();
    battle.droneAliveCount = 650; // 動作確認用の初期値（Commanderの戦術はHP割合で判定するため、この値自体は戦術選択に影響しない）


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



