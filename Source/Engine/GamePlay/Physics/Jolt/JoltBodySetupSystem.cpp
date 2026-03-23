#include "JoltBodySetupSystem.h"
#include "ECS/Core/CCL_World.h"
#include "Engine/Physics/JoltPhysicsManager.h"

#include "Engine/GamePlay/Physics/Jolt/JoltHandleComponent.h"
#include "Engine/GamePlay/Physics/Collision/JoltBoxColliderComponent.h"
#include "Engine/GamePlay/Physics/Collision/JoltSphereColliderComponent.h"
#include "Engine/GamePlay/Physics/Collision/JoltCapsuleColliderComponent.h"

#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <DirectXMath.h>

#include "ECS/System/CCL_SystemRegistry.h"
#include "Game/Core/SystemPriority.h"

using namespace DirectX;

namespace {
    // ヘルパー：Degree(度)から JoltのQuaternion への変換（UpdateSystemと同じもの）
    inline JPH::Quat EulerDegreeToJoltQuat(const XMFLOAT3& eulerDegree) {
        float pitch = XMConvertToRadians(eulerDegree.x);
        float yaw = XMConvertToRadians(eulerDegree.y);
        float roll = XMConvertToRadians(eulerDegree.z);
        return JPH::Quat::sEulerAngles(JPH::Vec3(pitch, yaw, roll));
    }
}

void JoltBodySetupSystem::Update(float dt)
{
    if (!_world->HasResource<JoltPhysicsManager>()) return;
    auto               &joltManager   = _world->GetResource<JoltPhysicsManager>();
    JPH::BodyInterface &bodyInterface = joltManager.GetBodyInterface();

    // ★あなたのエンジンの IfSystem の恩恵により、
    // Transform と Rigidbody を持っているエンティティ「だけ」がここに流れてくる！
    ForEachWithID([&](CCL::ECS::EntityID            id,
                      const TransformComponent     &trans,
                      const JoltRigidbodyComponent &rigid) {
        // 【唯一の除外条件】すでに実体ID(Handle)が作られているならスキップ
        if (_world->HasComponent<JoltHandleComponent>(id)) {
            return;
        }

        JPH::ShapeRefC shape = nullptr;

        // =======================================================
        // 1. Box Collider の生成
        // =======================================================
        if (auto* box = _world->GetComponent<JoltBoxColliderComponent>(id)) {
            JPH::BoxShapeSettings baseSettings(JPH::Vec3(box->halfExtent.x, box->halfExtent.y, box->halfExtent.z));
            JPH::ShapeRefC baseShape = baseSettings.Create().Get();

            // ★ オフセットと回転を適用したラッパー(包み紙)でくるむ
            JPH::RotatedTranslatedShapeSettings offsetSettings(
                JPH::Vec3(box->localOffset.x, box->localOffset.y, box->localOffset.z),
                EulerDegreeToJoltQuat(box->localRotationEuler),
                baseShape
            );
            shape = offsetSettings.Create().Get();
        }
        // =======================================================
        // 2. Sphere Collider の生成
        // =======================================================
        else if (auto* sphere = _world->GetComponent<JoltSphereColliderComponent>(id)) {
            JPH::SphereShapeSettings baseSettings(sphere->radius);
            JPH::ShapeRefC baseShape = baseSettings.Create().Get();

            // ★ 球は回転の影響を受けないため、位置ズレのみ適用
            JPH::RotatedTranslatedShapeSettings offsetSettings(
                JPH::Vec3(sphere->localOffset.x, sphere->localOffset.y, sphere->localOffset.z),
                JPH::Quat::sIdentity(),
                baseShape
            );
            shape = offsetSettings.Create().Get();
        }
        // =======================================================
        // 3. Capsule Collider の生成
        // =======================================================
        else if (auto* capsule = _world->GetComponent<JoltCapsuleColliderComponent>(id)) {
            JPH::CapsuleShapeSettings baseSettings(capsule->halfHeight, capsule->radius);
            JPH::ShapeRefC baseShape = baseSettings.Create().Get();

            // ★ オフセットと回転を適用
            JPH::RotatedTranslatedShapeSettings offsetSettings(
                JPH::Vec3(capsule->localOffset.x, capsule->localOffset.y, capsule->localOffset.z),
                EulerDegreeToJoltQuat(capsule->localRotationEuler),
                baseShape
            );
            shape = offsetSettings.Create().Get();
        }

        // コライダーコンポーネントが一つも無い場合は物理実体を作れないのでスキップ
        if (shape == nullptr) return;

        // 2. Jolt剛体の生成 (Rigidbodyコンポーネントの設定値を流し込む)
        JPH::BodyCreationSettings bodySettings(shape,
            JPH::RVec3(trans.position.x, trans.position.y, trans.position.z),
            JPH::Quat(trans.rotation.x, trans.rotation.y, trans.rotation.z, trans.rotation.w),
            rigid.motionType,
            rigid.objectLayer);
        bodySettings.mRestitution = rigid.restitution;
        bodySettings.mFriction    = rigid.friction;

        // =========================================================
        //  Joltに初速と詳細設定を叩き込む！
        // =========================================================
        bodySettings.mLinearVelocity = JPH::Vec3(rigid.initialVelocity.x, rigid.initialVelocity.y, rigid.initialVelocity.z);
        bodySettings.mGravityFactor  = rigid.gravityFactor;
        bodySettings.mIsSensor       = rigid.isSensor;

        // 弾が高速すぎて壁をすり抜ける（トンネリング）現象を防ぐ最高品質の計算
        if (rigid.useCCD) {
            bodySettings.mMotionQuality = JPH::EMotionQuality::LinearCast;
        }

        // =========================================================
        // 　Jolt剛体の記憶に、ECSのEntityIDを刻み込む！
        // =========================================================
        bodySettings.mUserData = static_cast<JPH::uint64>(id);

        JPH::Body *body = bodyInterface.CreateBody(bodySettings);

        // Static(静的)なものは最初からスリープ状態で空間に追加し、無駄な計算を省く
        JPH::EActivation activation = (rigid.motionType == JPH::EMotionType::Static)
                                          ? JPH::EActivation::DontActivate
                                          : JPH::EActivation::Activate;
        bodyInterface.AddBody(body->GetID(), activation);

        // 3. ECSに「Handle（実体ID）」を登録
        // これが付与されることで、次フレームからこの生成処理はスキップされ、
        // Pull / Push / Cleanup システムがこのエンティティを認識し始める。
        JoltHandleComponent handleComp;
        handleComp.bodyID = body->GetID();
        _world->AddComponent<JoltHandleComponent>(id, handleComp);
    });
}

// 登録順序: 物理計算が始まる前の PrePhysics フェーズで剛体を生成しておく必要がある。
REGISTER_LOGIC_SYSTEM(JoltBodySetupSystem, Priority::LogicStage::PrePhysics);