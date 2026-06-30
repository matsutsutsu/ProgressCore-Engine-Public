#include "ProtoDroneSwarmSystem.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include "Game/Prototype/PrototypeConfig.h"

using namespace CCL::ECS;
using namespace DirectX::SimpleMath;

void ProtoDroneSwarmSystem::Update(float dt)
{
    if (!_world->HasResource<PrototypeConfig>()) return;
    auto &config = _world->GetResource<PrototypeConfig>();

    // 1. プレイヤーの現在座標を取得
    Vector3 playerPos   = Vector3::Zero;
    bool    playerFound = false;


    auto playerView = _world->View<PlayerComponent, TransformComponent>();
    for (auto p : playerView)
    {
        auto* trans = _world->GetComponent<TransformComponent>(p);
        playerPos   = trans->position;
        playerFound = true;

        break;
    }

    if (!playerFound) return;

    // 2. 全ドローンの移動処理（並列化も容易な純粋数学計算）
    int activeDrones = 0;
    ForEach([&](TransformComponent &trans, ProtoDroneComponent &drone) {
        activeDrones++;

        Vector3 toPlayer = playerPos - trans.position;
        toPlayer.y       = 0.0f; // 平面移動
        float distSq     = toPlayer.LengthSquared();

        if (drone.currentState == ProtoDroneComponent::State::Approach)
        {
            // プレイヤーに近づく
            if (distSq > 0.001f)
            {
                toPlayer.Normalize();
                trans.position = trans.position + toPlayer * config.droneApproachSpeed * dt;

                // プレイヤーの方を向く
                float yaw      = atan2f(toPlayer.x, toPlayer.z);
                trans.rotation = Quaternion::CreateFromAxisAngle(Vector3::UnitY, yaw);
            }

            // 一定距離に入ったら突撃モードへ移行
            if (distSq < (config.droneDashRange * config.droneDashRange))
            {
                drone.currentState  = ProtoDroneComponent::State::DashAttack;
                drone.dashDirection = toPlayer;
                drone.dashDirection.Normalize();
                drone.stateTimer = 1.0f; // 1秒間突撃
            }
        }
        else if (drone.currentState == ProtoDroneComponent::State::DashAttack)
        {
            // 突撃実行
            trans.position = trans.position + drone.dashDirection * config.droneDashSpeed * dt;
            drone.stateTimer -= dt;

            // 突撃終了で再び接近モードへ
            if (drone.stateTimer <= 0.0f)
            {
                drone.currentState = ProtoDroneComponent::State::Approach;
            }
        }
    });

    // 統計情報を更新
    config.currentDroneCount = activeDrones;
}

REGISTER_LOGIC_SYSTEM(ProtoDroneSwarmSystem, Priority::LogicStage::L02_Update);