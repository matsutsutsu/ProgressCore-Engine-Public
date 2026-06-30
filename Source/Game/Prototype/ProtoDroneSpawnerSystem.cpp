#include "ProtoDroneSpawnerSystem.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/Common/CCL_Archetype.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

#include "Game/Prototype/PrototypeConfig.h"
#include "Game/Prototype/ProtoDroneComponent.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Game/Logics/Character/Player/Components/PlayerComponent.h"
#include "Engine/Graphics/Components/Core/PrimitiveComponent.h"
#include "Engine/Physics/Components/Collision/JoltSphereColliderComponent.h"
#include "Engine/Physics/Components/RigidBody/JoltRigidBodyComponent.h"

// ※必要に応じてモデルや描画用コンポーネントをインクルードしてください
// #include "Engine/Graphics/Components/Core/ModelComponent.h"

using namespace CCL::ECS;
using namespace DirectX::SimpleMath;

void ProtoDroneSpawnerSystem::Update(float /*dt*/)
{
    if (!_world->HasResource<PrototypeConfig>()) return;
    auto &config = _world->GetResource<PrototypeConfig>();

    if (!config.enableDroneSpawn) return;

    // 足りないドローンの数を計算
    int spawnCount = config.targetDroneCount - config.currentDroneCount;
    if (spawnCount <= 0) return;

    // プレイヤーの現在座標を取得
    Vector3 playerPos  = Vector3::Zero;
    auto    playerView = _world->View<PlayerComponent, TransformComponent>();
    for (auto p : playerView)
    {
        auto *trans = _world->GetComponent<TransformComponent>(p);
        playerPos   = trans->position;
        break;
    }

    // ドローン用アーキタイプの生成
    // ※ 描画が必要な場合は ModelComponent 等を追加してください
    static const Archetype droneArchetype = ArchetypeHelper::Generate<
        TransformComponent,
        PrimitiveComponent,
        JoltRigidbodyComponent,
        JoltSphereColliderComponent,
        ProtoDroneComponent>();

    // 不足分をスポーン
    for (int i = 0; i < spawnCount; ++i)
    {
        auto entityRef = _world->Spawn(droneArchetype);

        // プレイヤーから 半径40.0f 〜 50.0f のランダムな円周上にスポーン
        float angle    = static_cast<float>(rand() % 360) * (3.14159f / 180.0f);
        float distance = 40.0f + static_cast<float>(rand() % 10);

        Vector3 spawnPos;
        spawnPos.x = playerPos.x + std::cos(angle) * distance;
        spawnPos.y = 1.0f; // 地面から少し浮いた位置
        spawnPos.z = playerPos.z + std::sin(angle) * distance;

        TransformComponent trans{};
        trans.position = spawnPos;
        trans.rotation = Quaternion::Identity;

        ProtoDroneComponent drone{};
        // 初期状態は接近モード
        drone.currentState = ProtoDroneComponent::State::Approach;

        JoltRigidbodyComponent rigid{};
        rigid.motionType = JPH::EMotionType::Kinematic;
        rigid.objectLayer = PhysicsLayers::MOVING;

        entityRef.Set(trans);
        entityRef.Set(drone);
        entityRef.Set(rigid);
    }
}

// 毎フレームの最初の方で生成を行う
REGISTER_LOGIC_SYSTEM(ProtoDroneSpawnerSystem, Priority::LogicStage::L02_Update);