#include "JoltCharacterSetupSystem.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/Physics/JoltIntegration/JoltPhysicsManager.h"
#include "Engine/Physics/JoltIntegration/JoltShapeFactory.h"

#include "Engine/Physics/Components/Collision/JoltCapsuleColliderComponent.h"
#include "Engine/Physics/Components/Jolt/JoltHandleComponent.h" // プロキシ剛体の Handle

#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>

#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"



// まだ作っていないキャラクターに、CharacterVirtual とプロキシ剛体を作る
void JoltCharacterSetupSystem::Update(float dt)
{
    // 処理フロー:
    // 1. 足元を原点にしたカプセルの形を作る
    // 2. CharacterVirtual を作る
    // 3. 他者から見えるプロキシ剛体を作る

    if (!m_world->HasResource<JoltPhysicsManager *>()) return;
    auto               &joltManager   = m_world->GetResource<JoltPhysicsManager *>();
    JPH::PhysicsSystem *physicsSystem = joltManager->GetPhysicsSystem();
    JPH::BodyInterface &bodyInterface = joltManager->GetBodyInterface();

    ForEachWithID([&](CCL::ECS::EntityID                  id,
                      const TransformComponent           &trans,
                      const JoltCharacterConfigComponent &config) {
        // キャラクターはカプセルで作る
        // 角が無いので、段差や坂を滑らかに越えられる
        auto *capsule = m_world->GetComponent<JoltCapsuleColliderComponent>(id);
        if (!capsule) return;

        // 作る位置と向き。親を持つキャラだけ、ワールド座標に直す
        // 親が無ければローカルとワールドは同じ。
        // 生成したばかりで行列が未計算でも、ずれない
        JPH::RVec3 spawnPos(trans.position.x, trans.position.y, trans.position.z);
        JPH::Quat  spawnRot(trans.rotation.x, trans.rotation.y, trans.rotation.z, trans.rotation.w);
        if (trans.parent.IsValid())
        {
            const DirectX::XMFLOAT3 worldPos = trans.GetWorldPosition();
            const DirectX::XMFLOAT4 worldRot = trans.GetWorldRotation();
            spawnPos                         = JPH::RVec3(worldPos.x, worldPos.y, worldPos.z);
            spawnRot                         = JPH::Quat(worldRot.x, worldRot.y, worldRot.z, worldRot.w);
        }
        spawnRot = spawnRot.Normalized();

        // ---------------------------------------------------------
        // 1. カプセルの形
        // ---------------------------------------------------------

        // 足元を原点にしたカプセルを作る
        // CharacterVirtual とプロキシ剛体で同じ形を使い、当たり判定を一致させる
        JPH::RefConst<JPH::ShapeSettings> capsuleSettings =
            new JPH::CapsuleShapeSettings(capsule->halfHeight, capsule->radius);

        JPH::RefConst<JPH::ShapeSettings> rotTransSettings =
            new JPH::RotatedTranslatedShapeSettings(
                JPH::Vec3(0, capsule->halfHeight + capsule->radius, 0), // 足元→中心
                JPH::Quat::sIdentity(),
                capsuleSettings);

        JPH::RefConst<JPH::Shape> characterShape =
            JoltShapeFactory::CreateChecked(*rotTransSettings, static_cast<uint64_t>(id), "Character Capsule");
        if (characterShape == nullptr) return; // 失敗はログ済み。キャラを作らない

        // ---------------------------------------------------------
        // 2. CharacterVirtual
        // ---------------------------------------------------------

        JPH::CharacterVirtualSettings settings;
        settings.mShape                     = characterShape;
        settings.mSupportingVolume          = JPH::Plane(JPH::Vec3::sAxisY(), -capsule->radius); // 足元の判定面
        settings.mMaxSlopeAngle             = JPH::DegreesToRadians(config.maxSlopeAngle);
        settings.mMaxStrength               = config.characterMass * 10.0f; // 他の物体を押す力
        settings.mCharacterPadding          = 0.02f;
        settings.mPenetrationRecoverySpeed  = 1.0f;
        settings.mPredictiveContactDistance = 0.1f;

        auto character = new JPH::CharacterVirtual(&settings, spawnPos, spawnRot, physicsSystem);

        JoltCharacterHandleComponent handleComp;
        handleComp.character = character;
        m_world->AddComponent<JoltCharacterHandleComponent>(id, handleComp);

        // ---------------------------------------------------------
        // 3. プロキシ剛体
        // ---------------------------------------------------------

        // 他者（攻撃判定・レイ）から見える体を、同じ形の Kinematic 剛体で作る
        // CharacterVirtual はブロードフェーズに入らず、そのままでは他者から見えない
        JPH::BodyCreationSettings proxySettings(
            characterShape,
            spawnPos,
            spawnRot,
            JPH::EMotionType::Kinematic,
            config.objectLayer);

        // UserData に EntityID を入れ、接触やレイから Entity を引けるようにする
        proxySettings.mUserData = static_cast<JPH::uint64>(id);

        // 作れなかった（剛体の上限）ときはプロキシ無しで続ける
        // 移動は CharacterVirtual が担うので、動きはする
        JPH::Body *proxy = bodyInterface.CreateBody(proxySettings);
        if (proxy != nullptr)
        {
            bodyInterface.AddBody(proxy->GetID(), JPH::EActivation::Activate);

            JoltHandleComponent proxyHandle;
            proxyHandle.bodyID = proxy->GetID();
            m_world->AddComponent<JoltHandleComponent>(id, proxyHandle);
        }
    });
}



REGISTER_LOGIC_SYSTEM(JoltCharacterSetupSystem, Priority::LogicStage::L03_PrePhysics);
