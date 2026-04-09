#include "NavSteeringSystem.h"
#include "Engine/Physics/IPhysicsAPI.h"
#include <DirectXMath.h>

#include "ECS/System/CCL_SystemRegistry.h"
#include "Game/Core/SystemPriority.h"

void NavSteeringSystem::Update(float dt) {
    // _world は基底クラス(IfSystem)で保護されているメンバ変数を使用
    if (!_world || !_world->HasResource<std::shared_ptr<IPhysicsAPI>>()) return;

    std::shared_ptr<IPhysicsAPI> physics = _world->GetResource<std::shared_ptr<IPhysicsAPI>>();
    if (!physics) return;

    // ForEachを用いたベルトコンベア式のデータ処理
    // 物理エンジンへの命令に必要なため、第一引数で EntityID を受け取る
    ForEachWithID([&](CCL::ECS::EntityID entity, const TransformComponent& transform, NavAgentComponent& agent) {

        // 経路がない、または目的地に到達済みの場合はスキップ
        if (agent.waypoints.empty() || agent.currentWaypointIndex >= agent.waypoints.size()) {
            // 止める処理（ラムダ関数のため return で次のエンティティへ）
            physics->SetLinearVelocity(entity, { 0.0f, 0.0f, 0.0f });
            return;
        }

        // 次に向かうべきウェイポイント（目標座標）
        DirectX::XMFLOAT3 currentTarget = agent.waypoints[agent.currentWaypointIndex];

        // 1. 方向ベクトルの計算 (ポインタではなく参照なので . を使用)
        DirectX::XMVECTOR posVec = DirectX::XMLoadFloat3(&transform.position);
        DirectX::XMVECTOR targetVec = DirectX::XMLoadFloat3(&currentTarget);

        // Y軸（高さ）の差は無視する（平面上の移動として扱う）
        DirectX::XMVECTOR dirVec = DirectX::XMVectorSubtract(targetVec, posVec);
        dirVec = DirectX::XMVectorSetY(dirVec, 0.0f);

        // 2. 距離の計算と到達判定
        float distanceToTarget = DirectX::XMVectorGetX(DirectX::XMVector3Length(dirVec));

        const float ARRIVAL_RADIUS = 1.0f;
        if (distanceToTarget < ARRIVAL_RADIUS) {
            agent.currentWaypointIndex++;
            return; // 次のフレームで新しい目標へ向かうためスキップ
        }

        // 3. 移動ベクトルの計算と適用（Steering）
        const float MOVE_SPEED = 5.0f;
        dirVec = DirectX::XMVector3Normalize(dirVec);
        DirectX::XMVECTOR velVec = DirectX::XMVectorScale(dirVec, MOVE_SPEED);

        DirectX::XMFLOAT3 velocity;
        DirectX::XMStoreFloat3(&velocity, velVec);

        // 物理エンジン(Jolt)に速度を渡して歩かせる
        physics->SetLinearVelocity(entity, velocity);

        // 4. キャラクターを進行方向に向かせる
        DirectX::XMFLOAT3 direction;
        DirectX::XMStoreFloat3(&direction, dirVec);
        physics->SetLookDirection(entity, direction);
        });
}

REGISTER_LOGIC_SYSTEM(NavSteeringSystem, Priority::LogicStage::L04_Physics);