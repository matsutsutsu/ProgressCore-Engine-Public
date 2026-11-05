#include "JoltBodySetupSystem.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/Physics/JoltIntegration/JoltPhysicsManager.h"

#include "Engine/Physics/Components/Collision/JoltBoxColliderComponent.h"
#include "Engine/Physics/Components/Collision/JoltSphereColliderComponent.h"
#include "Engine/Physics/Components/Collision/JoltCapsuleColliderComponent.h"
#include "Engine/Physics/Components/Collision/JoltMeshColliderComponent.h"
#include "Engine/Graphics/Components/Core/ModelComponent.h"

#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/ScaledShape.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <DirectXMath.h>

#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include "Engine/Platform/Core/Logger.h"
#include "Engine/Physics/JoltIntegration/JoltShapeFactory.h"

using namespace DirectX;

void JoltBodySetupSystem::Update(float dt)
{
    if (!m_world->HasResource<JoltPhysicsManager *>()) return;
    auto               &joltManager   = m_world->GetResource<JoltPhysicsManager *>();
    JPH::BodyInterface &bodyInterface = joltManager->GetBodyInterface();

    // Transform と Rigidbody を両方持ち、JoltHandle を持たない Entity だけが渡ってくる
    ForEachWithID([&](CCL::ECS::EntityID            id,
                      const TransformComponent     &trans,
                      const JoltRigidbodyComponent &rigid) {
        // SetExclude と同じ条件の念のための確認（剛体の二重生成を防ぐ）
        if (m_world->HasComponent<JoltHandleComponent>(id))
        {
            return;
        }

        JPH::ShapeRefC shape = nullptr;

        // 1. Box Collider
        if (auto *box = m_world->GetComponent<JoltBoxColliderComponent>(id))
        {
            // ローカルの位置・回転でずらした形状で包むところまでまとめて行う
            JPH::BoxShapeSettings baseSettings(JPH::Vec3(box->halfExtent.x, box->halfExtent.y, box->halfExtent.z));
            shape = JoltShapeFactory::CreateOffsetShape(
                baseSettings, box->localOffset, box->localRotationEuler, id, "Box");
        }
        // 2. Sphere Collider
        else if (auto *sphere = m_world->GetComponent<JoltSphereColliderComponent>(id))
        {
            // 球は回転の影響を受けないため、位置ズレのみ適用
            JPH::SphereShapeSettings baseSettings(sphere->radius);
            shape = JoltShapeFactory::CreateOffsetShape(
                baseSettings, sphere->localOffset, {0.0f, 0.0f, 0.0f}, id, "Sphere");
        }
        // 3. Capsule Collider
        else if (auto *capsule = m_world->GetComponent<JoltCapsuleColliderComponent>(id))
        {
            JPH::CapsuleShapeSettings baseSettings(capsule->halfHeight, capsule->radius);
            shape = JoltShapeFactory::CreateOffsetShape(
                baseSettings, capsule->localOffset, capsule->localRotationEuler, id, "Capsule");
        }
        // 4. Mesh Collider（地形）: 描画用モデルの頂点から形状を作る
        else if (auto *meshCol = m_world->GetComponent<JoltMeshColliderComponent>(id))
        {
            // 4-1. 同じ Entity の描画用モデルを取得
            auto *modelComp = m_world->GetComponent<ModelComponent>(id);

            // モデルがない、ロードされていない、または無効ならスキップ
            if (!modelComp || !modelComp->GetModel() || !meshCol->isEnabled) return;

            // リソース（CPU上の頂点データ）にアクセス
            const ModelResource *res = modelComp->GetModel()->GetResource();
            if (!res) return;

            JPH::VertexList          joltVertices;
            JPH::IndexedTriangleList joltTriangles;

            // 4-2. ModelResource 内の全メッシュから頂点とインデックスを集める
            for (const auto &mesh : res->GetMeshes())
            {
                uint32_t vertexOffset = static_cast<uint32_t>(joltVertices.size());

                // 物理に要るのは位置だけ（UV や法線は使わない）
                for (const auto &v : mesh.vertices)
                {
                    joltVertices.push_back(JPH::Float3(v.position.x, v.position.y, v.position.z));
                }

                // インデックスを抽出（複数のメッシュを1つに合体させるためオフセットを足す）
                for (size_t i = 0; i < mesh.indices.size(); i += 3)
                {
                    joltTriangles.push_back(JPH::IndexedTriangle(
                        mesh.indices[i + 0] + vertexOffset,
                        mesh.indices[i + 1] + vertexOffset,
                        mesh.indices[i + 2] + vertexOffset));
                }
            }

            if (joltTriangles.empty()) return;

            // 4-3. スケールを掛ける前のメッシュ形状を作る
            JPH::MeshShapeSettings meshSettings(joltVertices, joltTriangles);
            JPH::ShapeRefC         rawMeshShape = JoltShapeFactory::CreateChecked(meshSettings, id, "Mesh");
            if (rawMeshShape == nullptr) return;

            // 4-4. Transform のスケールを形状に掛ける
            // Jolt の剛体は Transform のスケールを持たないので、ここで掛けないと
            // 見た目だけ拡大して当たり判定は元の大きさのままになる
            JPH::ScaledShapeSettings scaledSettings(
                rawMeshShape,
                JPH::Vec3(trans.scale.x, trans.scale.y, trans.scale.z));
            shape = JoltShapeFactory::CreateChecked(scaledSettings, id, "Mesh(scaled)");
        }

        // コライダーコンポーネントが一つもない場合は物理実体を作れないのでスキップ
        if (shape == nullptr) return;

        // 剛体の生成（JoltRigidbodyComponent の設定値を渡す）
        //  ローカルの trans.position ではなくワールド座標で生成する
        DirectX::XMFLOAT3 worldPos = trans.GetWorldPosition();
        DirectX::XMFLOAT4 worldRot = trans.GetWorldRotation();

        JPH::Quat initRot(worldRot.x, worldRot.y, worldRot.z, worldRot.w);
        initRot = initRot.Normalized(); // 正規化されていない回転を渡すと Jolt のアサートや不安定な挙動になる

        JPH::BodyCreationSettings bodySettings(shape,
                                               JPH::RVec3(worldPos.x, worldPos.y, worldPos.z),
                                               initRot,
                                               rigid.motionType,
                                               rigid.objectLayer);
        bodySettings.mRestitution = rigid.restitution;
        bodySettings.mFriction    = rigid.friction;

        // 初速と詳細設定
        bodySettings.mLinearVelocity = JPH::Vec3(rigid.initialVelocity.x, rigid.initialVelocity.y, rigid.initialVelocity.z);
        bodySettings.mGravityFactor  = rigid.gravityFactor;
        bodySettings.mIsSensor       = rigid.isSensor;

        // 高速な物体が壁をすり抜ける（トンネリング）のを防ぐ連続衝突判定
        if (rigid.useCCD)
        {
            bodySettings.mMotionQuality = JPH::EMotionQuality::LinearCast;
        }

        // Kinematic のセンサー（攻撃判定）が、Kinematic/Static のボディとの接触でもコールバックを出すようにする
        if (rigid.isSensor && rigid.motionType == JPH::EMotionType::Kinematic)
        {
            bodySettings.mCollideKinematicVsNonDynamic = true;
        }

        // 接触やレイキャストから Entity を引けるよう、UserData に EntityID を入れる
        bodySettings.mUserData = static_cast<JPH::uint64>(id);

        JPH::Body *body = bodyInterface.CreateBody(bodySettings);

        // 剛体の生成に失敗した場合（上限到達など）
        if (body == nullptr)
        {
            // 何も出さずに return してはいけない。ここを踏むと「一部のオブジェクトだけ
            //   当たり判定がない」という原因を追いにくい症状になる
            //   特に Debug は cMaxBodies=1024（Release は 65536）なので、
            //   Debug でだけ上限に触れるケースが実際に起こりうる
            CCL_LOG_ERROR(LogCategory::Physics,
                          "Jolt剛体の生成に失敗しました (Entity %llu)。ボディ数の上限に達した可能性があります",
                          static_cast<unsigned long long>(id));
            return; // この Entity はスキップして次へ
        }

        // Static は最初からスリープ状態で追加し、無駄な計算を省く
        JPH::EActivation activation = (rigid.motionType == JPH::EMotionType::Static)
                                          ? JPH::EActivation::DontActivate
                                          : JPH::EActivation::Activate;
        bodyInterface.AddBody(body->GetID(), activation);

        // JoltHandleComponent を付ける。これで次のステップから生成の対象外になり、
        // Pull / Push / Cleanup の各 System がこの Entity を扱い始める
        JoltHandleComponent handleComp;
        handleComp.bodyID = body->GetID();
        m_world->AddComponent<JoltHandleComponent>(id, handleComp);
    });
}



// 物理計算（L04）より前に剛体を生成しておく
REGISTER_LOGIC_SYSTEM(JoltBodySetupSystem, Priority::LogicStage::L03_PrePhysics);