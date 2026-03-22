#include "EnemyHitTestSystem.h"
#include "ECS/Core/CCL_World.h"
#include "Engine/Physics/JoltPhysicsManager.h"
#include "EnemyComponent.h"
#include "Engine/GamePlay/Physics/Character/JoltCharacterConfigComponent.h"
#include "Engine/GamePlay/Physics/Jolt/JoltHandleComponent.h"

#include "ECS/System/CCL_SystemRegistry.h"
#include "Game/Core/SystemPriority.h"

void EnemyHitTestSystem::Initialize() {
    // ECSのEventBusに対し、「JoltCollisionEvent が発行されたら OnCollision を呼んでくれ」と予約する
    _world->GetEventBus().Subscribe<JoltCollisionEvent>(
       [this](const JoltCollisionEvent &ev) { OnCollision(ev); });

      
}

void EnemyHitTestSystem::OnCollision(const JoltCollisionEvent& ev) {
    if (!_world->HasResource<JoltPhysicsManager>()) return;
    auto &joltManager = _world->GetResource<JoltPhysicsManager>();
    JPH::BodyInterface &bodyInterface = joltManager.GetBodyInterface();

    // 1. ぶつかった2つのEntityが「敵」と「プレイヤー」であるか確認する
    bool isEnemyA  = _world->HasComponent<EnemyComponent>(ev.entityA);
    bool isEnemyB  = _world->HasComponent<EnemyComponent>(ev.entityB);
    bool isPlayerA = _world->HasComponent<JoltCharacterConfigComponent>(ev.entityA);
    bool isPlayerB = _world->HasComponent<JoltCharacterConfigComponent>(ev.entityB);

    CCL::ECS::EntityID enemyID = 0;
    if (isEnemyA && isPlayerB) enemyID = ev.entityA; // Aが敵だった
    if (isEnemyB && isPlayerA) enemyID = ev.entityB; // Bが敵だった

    // 2. 敵とプレイヤーの衝突だった場合、敵を上空にカチ上げる！
    if (enemyID != 0) {
        auto* enemyComp = _world->GetComponent<EnemyComponent>(enemyID);
        
        // まだ吹き飛ばされていない場合のみ処理（連続ヒット防止）
        if (enemyComp && !enemyComp->isBlownAway) {
            enemyComp->isBlownAway = true; // AIの追従をストップさせる

            auto* handle = _world->GetComponent<JoltHandleComponent>(enemyID);
            if (handle) {
                // 現在の速度を取得し、Y軸（上方向）に強烈な初速「15.0f」を上書き！
                JPH::Vec3 currentVel = bodyInterface.GetLinearVelocity(handle->bodyID);
                bodyInterface.SetLinearVelocity(handle->bodyID, JPH::Vec3(currentVel.GetX(), 15.0f, currentVel.GetZ()));
                bodyInterface.ActivateBody(handle->bodyID);
            }
        }
    }
}

// 登録自体は必要（Initialize()をエンジン側から呼んでもらうため）
REGISTER_LOGIC_SYSTEM(EnemyHitTestSystem, Priority::LogicStage::Update);