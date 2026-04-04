#include "EnemyMoveSystem.h"
#include "ECS/Core/CCL_World.h"
#include "Engine/Physics/JoltPhysicsManager.h"
#include "Engine/GamePlay/Physics/Character/JoltCharacterConfigComponent.h" // プレイヤー判定用

#include "ECS/System/CCL_SystemRegistry.h"
#include "Game/Core/SystemPriority.h"

void EnemyMoveSystem::Update(float dt) {
    if (!_world->HasResource<JoltPhysicsManager>()) return;
    auto &joltManager = _world->GetResource<JoltPhysicsManager>();
    JPH::BodyInterface &bodyInterface = joltManager.GetBodyInterface();

    // 1. プレイヤー（CharacterConfigを持つEntity）のワールド座標を探す
    DirectX::XMFLOAT3 playerPos = {0, 0, 0};
    bool playerFound = false;
    auto playerView = _world->View<TransformComponent, JoltCharacterConfigComponent>();

    CCL::ECS::EntityID pid;
    if (playerView.empty()) return; // プレイヤーがいないなら何もしない

    pid = playerView[0]; // 最初に見つかったプレイヤーをターゲットにする（複数いる場合は要注意）

    if (auto *t = _world->GetComponent<TransformComponent>(pid)) {
      playerPos   = t->position;
      playerFound = true;
    }

    if (!playerFound) return;

    // 2. すべての敵をプレイヤーの方向へ動かす
    ForEachWithIDParallel([&](CCL::ECS::EntityID id, const TransformComponent &trans, EnemyComponent &enemy, const JoltHandleComponent &handle) {
        
        // =======================================================
        // ★ 吹き飛ばされ中の処理（復活判定）
        // =======================================================
        if (enemy.isBlownAway) {
            // 現在の速度を取得
            JPH::Vec3 vel = bodyInterface.GetLinearVelocity(handle.bodyID);
            

            // 速度がほぼゼロになったら（地面に落ちて転がったら）復活！
            if (vel.LengthSq() < 2.0f) {
                enemy.isBlownAway = false;
                
                // 倒れた体を強制的に「直立（Identity）」に戻す
                bodyInterface.SetRotation(handle.bodyID, JPH::Quat::sIdentity(), JPH::EActivation::Activate);
                bodyInterface.SetAngularVelocity(handle.bodyID, JPH::Vec3::sZero());
            }
            return; // 復活したフレームはここで終了し、次フレームから追従再開
        }

        // =======================================================
        // ★ 通常の追従処理（直立不動の維持）
        // =======================================================
        // カプセルが転がるのを防ぐため、毎フレーム「回転する力（角速度）」を殺す！
        bodyInterface.SetAngularVelocity(handle.bodyID, JPH::Vec3::sZero());

        // (以下、既存の方向計算と SetLinearVelocity のコードをそのまま配置...)
        using namespace DirectX;
        XMVECTOR pPos = XMLoadFloat3(&playerPos);
        XMVECTOR ePos = XMLoadFloat3(&trans.position);
        XMVECTOR dir  = XMVectorSubtract(pPos, ePos);
        
        dir = XMVectorSetY(dir, 0.0f);
        dir = XMVector3Normalize(dir);

        XMFLOAT3 moveDir;
        XMStoreFloat3(&moveDir, dir);

        JPH::Vec3 currentVel = bodyInterface.GetLinearVelocity(handle.bodyID);
        JPH::Vec3 newVel(moveDir.x * enemy.speed, currentVel.GetY(), moveDir.z * enemy.speed);

        bodyInterface.SetLinearVelocity(handle.bodyID, newVel);
        bodyInterface.ActivateBody(handle.bodyID);
    });
}

// 物理計算が始まる前（Updateフェーズ）にAIの意思決定を終わらせる
REGISTER_LOGIC_SYSTEM(EnemyMoveSystem, Priority::LogicStage::L02_Update);