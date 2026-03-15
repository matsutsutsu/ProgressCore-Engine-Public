#include "EnemyDeathSystem.h"
#include "Engine/GamePlay/Graphics/Core/DissolveComponent.h"
#include "Engine/GamePlay/Graphics/Core/MaterialComponent.h"
#include "Engine/GamePlay/Graphics/Core/ModelComponent.h"
#include "Engine/Graphics/Resource/ResourceManager.h"
#include "Game/Logics/Character/Enemy/BossComponent.h"
#include "Game/Logics/GameEvent/GameEvents.h"

#include "Engine/GamePlay/Physics/RigidBody/RigidBodyComponent.h"
#include "Game/Logics/Combat/HealthComponent.h"
#include "Engine/GamePlay/Physics/Collision/BoxColliderComponent.h"
#include "Engine/GamePlay/Physics/Collision/SphereColliderComponent.h"
#include "Engine/GamePlay/Physics/Collision/CapsuleColliderComponent.h"

// システムの実行順序の定義ヘッダー
#include "ECS/System/CCL_SystemRegistry.h"
#include "Game/Core/SystemPriority.h"

void EnemyDeathSystem::Update(float dt)
{
    // ForEachWithID内でコンポーネントの追加・削除を行うため、対象をリスト化して遅延実行する
    std::vector<CCL::ECS::EntityID> processedEntities;

    ForEachWithID([&](CCL::ECS::EntityID  id,
                      const EnemyComponent     &enemy,
                      const Tag::DeadTag       &dead,
                      const TransformComponent &trans) {
        // 1. コイン生成（イベント発行）
        EntityKilledEvent deathEvent;
        deathEvent.killedEntityID    = id;
        deathEvent.position          = trans.position;
        deathEvent.isPlayer          = false;
        deathEvent.droppedCoinAmount = enemy.dropMoney;
        _world->GetEventBus().Publish(deathEvent);


        // 2. ディゾルブ演出のセットアップ（ボス以外）
        if (!_world->HasComponent<BossComponent>(id)) {

            // ★コンポーネントを付けるだけ！マテリアルの複製は一切しない！
            DissolveComponent dissolve;
            dissolve.dissolveSpeed = 0.8f;
            _world->AddComponent<DissolveComponent>(id, dissolve);
        }


        // 処理済みリストに追加
        processedEntities.push_back(id);
    });

    // 3. 処理済みエンティティから EnemyComponent と HealthComponent を剥奪する
    // 以降の消滅管理は DissolveSystem が行い、閾値1.0で Destroy される
    for (auto id : processedEntities) {
        // 知能(Enemy)と体力(Health)を奪い、ただの「死体」にする。
        // これにより、次のフレームから EnemyBehaviorSystem や HealthSystem
        // から完全に無視されるようになります。
        _world->RequestRemoveComponent<EnemyComponent>(id);
        _world->RequestRemoveComponent<HealthComponent>(id);

        // ② 質量(RigidBody)と肉体(Collider)を奪う
        // ※ 持っていないコンポーネントの Remove 要求は安全に無視されるため、全部書いてOKです
        _world->RequestRemoveComponent<RigidBodyComponent>(id);
        _world->RequestRemoveComponent<SphereColliderComponent>(id);
        _world->RequestRemoveComponent<CapsuleColliderComponent>(id);
        _world->RequestRemoveComponent<BoxColliderComponent>(id);

    }
}


REGISTER_LOGIC_SYSTEM(EnemyDeathSystem, Priority::LogicStage::Cleanup);