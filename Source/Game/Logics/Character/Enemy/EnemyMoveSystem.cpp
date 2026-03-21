#include "EnemyMoveSystem.h"
#include "ECS/Core/CCL_World.h"
#include "Engine/Physics/JoltPhysicsManager.h"
#include "Game/Logics/Character/Player/PlayerComponent.h" // プレイヤーの位置取得用

#include "ECS/System/CCL_SystemRegistry.h"
#include "Game/Core/SystemPriority.h"

void EnemyMoveSystem::Update(float dt)
{
    if (!_world->HasResource<JoltPhysicsManager>()) return;
    auto &joltManager = _world->GetResource<JoltPhysicsManager>();
    JPH::BodyInterface &bodyInterface = joltManager.GetBodyInterface();

    // -------------------------------------------------------------
    // 1. 標的（プレイヤー）の座標を特定する (O(1)の速度で完了)
    // -------------------------------------------------------------
    DirectX::XMFLOAT3 playerPos = {0.0f, 0.0f, 0.0f};
    bool foundPlayer = false;

    auto playerView = _world->View<PlayerComponent, TransformComponent>();
    if (!playerView.empty()) {
        CCL::ECS::EntityID pid = playerView[0];
        if (auto* t = _world->GetComponent<TransformComponent>(pid)) {
            playerPos = t->position;
            foundPlayer = true;
        }
    }

    if (!foundPlayer) return; // プレイヤーがいなければ棒立ち

    JPH::Vec3 targetPos(playerPos.x, playerPos.y, playerPos.z);

    // -------------------------------------------------------------
    // 2. 超並列処理で全敵の速度ベクトルを上書きする
    // -------------------------------------------------------------
    ForEachParallel([&](const TransformComponent& trans, const EnemyComponent& enemy, const JoltHandleComponent& handle) {
        
        JPH::Vec3 currentPos(trans.position.x, trans.position.y, trans.position.z);
        JPH::Vec3 dir = targetPos - currentPos;
        dir.SetY(0.0f); // 空を飛ばないように水平方向の距離だけ見る

        float lengthSq = dir.LengthSq();
        if (lengthSq > 0.1f) {
            dir = dir / std::sqrt(lengthSq); // 正規化（方向だけのベクトルにする）

            // 今のY軸の速度(重力による落下)はそのまま維持する
            JPH::Vec3 currentVel = bodyInterface.GetLinearVelocity(handle.bodyID);
            JPH::Vec3 newVel = dir * enemy.moveSpeed;
            newVel.SetY(currentVel.GetY());

            // 速度を上書き。スリープ状態なら自動でActivate(叩き起こす)してくれる
            bodyInterface.SetLinearVelocity(handle.bodyID, newVel);
        }
    });
}

// 物理演算（JoltStepSystem）が走る前の【PrePhysics】で実行し、Joltに初速を託す
REGISTER_LOGIC_SYSTEM(EnemyMoveSystem, Priority::LogicStage::PrePhysics);