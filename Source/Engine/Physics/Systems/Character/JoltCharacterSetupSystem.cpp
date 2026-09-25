#include "JoltCharacterSetupSystem.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/Physics/JoltIntegration/JoltPhysicsManager.h"
#include "Engine/Physics/JoltIntegration/JoltShapeFactory.h"

#include "Engine/Physics/Components/Collision/JoltCapsuleColliderComponent.h"
#include "Engine/Physics/Components/Jolt/JoltHandleComponent.h" // presence プロキシの Handle

#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>

#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

void JoltCharacterSetupSystem::Update(float dt)
{
    if (!m_world->HasResource<JoltPhysicsManager *>()) return;
    auto               &joltManager   = m_world->GetResource<JoltPhysicsManager *>();
    JPH::PhysicsSystem *physicsSystem = joltManager->GetPhysicsSystem();
    JPH::BodyInterface &bodyInterface = joltManager->GetBodyInterface();

    ForEachWithID([&](CCL::ECS::EntityID                  id,
                      const TransformComponent           &trans,
                      const JoltCharacterConfigComponent &config) {
        // キャラクターはカプセル形状で作る（角が無いので段差や坂を滑らかに越えられる）
        auto *capsule = m_world->GetComponent<JoltCapsuleColliderComponent>(id);
        if (!capsule) return;

        // 1. カプセル形状の作成（足元を原点に合わせるオフセット付き）。
        //    この characterShape を CharacterVirtual と presence プロキシで共有し、
        //    両者の当たり判定を一致させる。
        JPH::RefConst<JPH::ShapeSettings> capsuleSettings =
            new JPH::CapsuleShapeSettings(capsule->halfHeight, capsule->radius);

        JPH::RefConst<JPH::ShapeSettings> rotTransSettings =
            new JPH::RotatedTranslatedShapeSettings(
                JPH::Vec3(0, capsule->halfHeight + capsule->radius, 0), // 足元→中心オフセット
                JPH::Quat::sIdentity(),
                capsuleSettings);

        JPH::RefConst<JPH::Shape> characterShape =
            JoltShapeFactory::CreateChecked(*rotTransSettings, static_cast<uint64_t>(id), "Character Capsule");
        if (characterShape == nullptr) return; // 失敗はログ済み。キャラを作らずスキップする

        // 2. CharacterVirtual の設定と生成
        JPH::CharacterVirtualSettings settings;
        settings.mShape                     = characterShape;
        settings.mSupportingVolume          = JPH::Plane(JPH::Vec3::sAxisY(), -capsule->radius); // 足元の判定面
        settings.mMaxSlopeAngle             = JPH::DegreesToRadians(config.maxSlopeAngle);
        settings.mMaxStrength               = config.characterMass * 10.0f; // 他の物体を押す力
        settings.mCharacterPadding          = 0.02f;
        settings.mPenetrationRecoverySpeed  = 1.0f;
        settings.mPredictiveContactDistance = 0.1f;

        auto character = new JPH::CharacterVirtual(&settings,
                                                   JPH::RVec3(trans.position.x, trans.position.y, trans.position.z),
                                                   JPH::Quat(trans.rotation.x, trans.rotation.y, trans.rotation.z, trans.rotation.w),
                                                   physicsSystem);

        JoltCharacterHandleComponent handleComp;
        handleComp.character = character;
        m_world->AddComponent<JoltCharacterHandleComponent>(id, handleComp);

        // 3. presence プロキシ（Kinematic 剛体）の生成。
        //    CharacterVirtual はブロードフェーズに入らないので、他者（攻撃判定/レイ/敵）から
        //    見えない。そこで同じ形状の Kinematic 剛体を1つ持たせ、毎フレーム
        //    CharacterVirtual に追従させることで「他者から見える体」を用意する。
        //    - レイヤーは config.objectLayer（剛体と同じ ObjectLayer 体系）
        //    - UserData に EntityID を入れ、レイキャスト/接触から Entity を引けるようにする
        //    - CharacterVirtual 側は UpdatePhysicsAndSync でこのプロキシを無視する
        JPH::Quat initRot(trans.rotation.x, trans.rotation.y, trans.rotation.z, trans.rotation.w);
        initRot = initRot.Normalized();

        JPH::BodyCreationSettings proxySettings(
            characterShape,
            JPH::RVec3(trans.position.x, trans.position.y, trans.position.z),
            initRot,
            JPH::EMotionType::Kinematic,
            config.objectLayer);
        proxySettings.mUserData = static_cast<JPH::uint64>(id);

        JPH::Body *proxy = bodyInterface.CreateBody(proxySettings);
        if (proxy != nullptr)
        {
            bodyInterface.AddBody(proxy->GetID(), JPH::EActivation::Activate);

            JoltHandleComponent proxyHandle;
            proxyHandle.bodyID = proxy->GetID();
            m_world->AddComponent<JoltHandleComponent>(id, proxyHandle);
        }
        // 生成失敗（上限到達）時は presence なしで続行（移動は CharacterVirtual が担うため動作はする）
    });
}

REGISTER_LOGIC_SYSTEM(JoltCharacterSetupSystem, Priority::LogicStage::L03_PrePhysics);