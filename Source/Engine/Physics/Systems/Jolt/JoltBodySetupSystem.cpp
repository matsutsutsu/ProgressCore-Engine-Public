#include "JoltBodySetupSystem.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/Physics/JoltIntegration/JoltPhysicsManager.h"


#include "Engine/Physics/Components/Collision/JoltBoxColliderComponent.h"
#include "Engine/Physics/Components/Collision/JoltSphereColliderComponent.h"
#include "Engine/Physics/Components/Collision/JoltCapsuleColliderComponent.h"
#include "Engine/Physics/Components/Collision/JoltMeshColliderComponent.h"
#include "Engine/Graphics/Components/Core/ModelComponent.h"


#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/ScaledShape.h> // スケール適用に必須
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

namespace {
    // ヘルパー：Degree(度)から JoltのQuaternion への変換（UpdateSystemと同じもの）
    inline JPH::Quat EulerDegreeToJoltQuat(const XMFLOAT3& eulerDegree) {
        float pitch = XMConvertToRadians(eulerDegree.x);
        float yaw = XMConvertToRadians(eulerDegree.y);
        float roll = XMConvertToRadians(eulerDegree.z);
        return JPH::Quat::sEulerAngles(JPH::Vec3(pitch, yaw, roll));
    }
}

JPH::ShapeRefC JoltBodySetupSystem::CreateShapeChecked(
    const JPH::ShapeSettings &settings, CCL::ECS::EntityID id, const char *what)
{
    // 検証の実体は JoltShapeFactory に集約してある（ShapeUpdate / CharacterSetup と共有）。
    return JoltShapeFactory::CreateChecked(settings, static_cast<uint64_t>(id), what);
}

void JoltBodySetupSystem::Update(float dt)
{
    if (!m_world->HasResource<JoltPhysicsManager*>()) return;
    auto               &joltManager   = m_world->GetResource<JoltPhysicsManager*>();
    JPH::BodyInterface &bodyInterface = joltManager->GetBodyInterface();

    // IfSystem の型フィルタにより、Transform と Rigidbody を併せ持つ個体だけが渡ってくる。
    // そのため、ここでの所持チェックは不要。
    ForEachWithID([&](CCL::ECS::EntityID            id,
                      const TransformComponent     &trans,
                      const JoltRigidbodyComponent &rigid) {
        // 【唯一の除外条件】すでに実体ID(Handle)が作られているならスキップ
        if (m_world->HasComponent<JoltHandleComponent>(id)) {
            return;
        }

        JPH::ShapeRefC shape = nullptr;

        // =======================================================
        // 1. Box Collider の生成
        // =======================================================
        if (auto* box = m_world->GetComponent<JoltBoxColliderComponent>(id)) {
            JPH::BoxShapeSettings baseSettings(JPH::Vec3(box->halfExtent.x, box->halfExtent.y, box->halfExtent.z));
            JPH::ShapeRefC baseShape = CreateShapeChecked(baseSettings, id, "Box");
            if (baseShape == nullptr) return;

            // オフセットと回転を適用したラッパー(包み紙)でくるむ
            JPH::RotatedTranslatedShapeSettings offsetSettings(
                JPH::Vec3(box->localOffset.x, box->localOffset.y, box->localOffset.z),
                EulerDegreeToJoltQuat(box->localRotationEuler),
                baseShape
            );
            shape = CreateShapeChecked(offsetSettings, id, "Box(offset)");
        }
        // =======================================================
        // 2. Sphere Collider の生成
        // =======================================================
        else if (auto* sphere = m_world->GetComponent<JoltSphereColliderComponent>(id)) {
            JPH::SphereShapeSettings baseSettings(sphere->radius);
            JPH::ShapeRefC baseShape = CreateShapeChecked(baseSettings, id, "Sphere");
            if (baseShape == nullptr) return;

            // 球は回転の影響を受けないため、位置ズレのみ適用
            JPH::RotatedTranslatedShapeSettings offsetSettings(
                JPH::Vec3(sphere->localOffset.x, sphere->localOffset.y, sphere->localOffset.z),
                JPH::Quat::sIdentity(),
                baseShape
            );
            shape = CreateShapeChecked(offsetSettings, id, "Sphere(offset)");
        }
        // =======================================================
        // 3. Capsule Collider の生成
        // =======================================================
        else if (auto* capsule = m_world->GetComponent<JoltCapsuleColliderComponent>(id)) {
            JPH::CapsuleShapeSettings baseSettings(capsule->halfHeight, capsule->radius);
            JPH::ShapeRefC baseShape = CreateShapeChecked(baseSettings, id, "Capsule");
            if (baseShape == nullptr) return;

            // オフセットと回転を適用
            JPH::RotatedTranslatedShapeSettings offsetSettings(
                JPH::Vec3(capsule->localOffset.x, capsule->localOffset.y, capsule->localOffset.z),
                EulerDegreeToJoltQuat(capsule->localRotationEuler),
                baseShape
            );
            shape = CreateShapeChecked(offsetSettings, id, "Capsule(offset)");
        }
        // =======================================================
        // 4. Mesh Collider (地形) の自動抽出と生成
        // =======================================================
        else if (auto* meshCol = m_world->GetComponent<JoltMeshColliderComponent>(id)) {

            // 1. 同じEntityから、描画用のモデルコンポーネントを取得
            auto* modelComp = m_world->GetComponent<ModelComponent>(id);

            // モデルが無い、ロードされていない、またはタグがOFFならスキップ
            if (!modelComp || !modelComp->GetModel() || !meshCol->isEnabled) return;

            // リソース（CPU上の頂点データ）にアクセス
            const ModelResource* res = modelComp->GetModel()->GetResource();
            if (!res) return;

            JPH::VertexList joltVertices;
            JPH::IndexedTriangleList joltTriangles;

            // 2. ModelResource内の全メッシュから頂点とインデックスを吸い出す
            for (const auto& mesh : res->GetMeshes()) {
                uint32_t vertexOffset = static_cast<uint32_t>(joltVertices.size());

                // 頂点の「位置座標(Position)」だけを抽出 (UVや法線は物理には不要！)
                for (const auto& v : mesh.vertices) {
                    joltVertices.push_back(JPH::Float3(v.position.x, v.position.y, v.position.z));
                }

                // インデックスを抽出（複数のメッシュを1つに合体させるためオフセットを足す）
                for (size_t i = 0; i < mesh.indices.size(); i += 3) {
                    joltTriangles.push_back(JPH::IndexedTriangle(
                        mesh.indices[i + 0] + vertexOffset,
                        mesh.indices[i + 1] + vertexOffset,
                        mesh.indices[i + 2] + vertexOffset
                    ));
                }
            }

            if (joltTriangles.empty()) return;

            // 3. Joltの「純粋なメッシュ形状」を作成
            JPH::MeshShapeSettings meshSettings(joltVertices, joltTriangles);
            JPH::ShapeRefC rawMeshShape = CreateShapeChecked(meshSettings, id, "Mesh");
            if (rawMeshShape == nullptr) return;

            // 4. 最重要: Transform のスケールを物理メッシュに適用する！
            // エディタで家を「3倍」に拡大した場合、物理判定も「3倍」に膨らませる
            JPH::ScaledShapeSettings scaledSettings(
                rawMeshShape,
                JPH::Vec3(trans.scale.x, trans.scale.y, trans.scale.z)
            );
            shape = CreateShapeChecked(scaledSettings, id, "Mesh(scaled)");
        }

        // コライダーコンポーネントが一つも無い場合は物理実体を作れないのでスキップ
        if (shape == nullptr) return;

        // 2. Jolt剛体の生成 (Rigidbodyコンポーネントの設定値を流し込む)
        //  ローカル座標(trans.position)ではなく、絶対座標(WorldPosition)で生成する！
        DirectX::XMFLOAT3 worldPos = trans.GetWorldPosition();
        DirectX::XMFLOAT4 worldRot = trans.GetWorldRotation();

        JPH::Quat initRot(worldRot.x, worldRot.y, worldRot.z, worldRot.w);
        initRot = initRot.Normalized(); // Joltが荒ぶるのを防ぐための必須処理(正規化)

        JPH::BodyCreationSettings bodySettings(shape,
            JPH::RVec3(worldPos.x, worldPos.y, worldPos.z),
            initRot,
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

        // センサー（弾）が、Kinematicな敵に当たった時にもコールバックを発生させる設定
        if (rigid.isSensor && rigid.motionType == JPH::EMotionType::Kinematic) {
            bodySettings.mCollideKinematicVsNonDynamic = true;
        }

        // =========================================================
        // 　Jolt剛体の記憶に、ECSのEntityIDを刻み込む！
        // =========================================================
        bodySettings.mUserData = static_cast<JPH::uint64>(id);

        JPH::Body *body = bodyInterface.CreateBody(bodySettings);

        // 剛体の生成に失敗した場合（上限到達など）のフェイルセーフ
        if (body == nullptr)
        {
            // 無言で return してはいけない。ここを踏むと「一部のオブジェクトだけ
            //   当たり判定が無い」という極めて追いにくい症状になる。
            //   特に Debug は cMaxBodies=1024（Release は 65536）なので、
            //   Debug でだけ上限に触れるケースが実際に起こりうる。
            CCL_LOG_ERROR(LogCategory::Physics,
                "Jolt剛体の生成に失敗しました (Entity %llu)。ボディ数の上限に達した可能性があります",
                static_cast<unsigned long long>(id));
            return; // このエンティティの生成をスキップして、次のループへ
        }

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
        m_world->AddComponent<JoltHandleComponent>(id, handleComp);
    });
}

// 登録順序: 物理計算が始まる前の PrePhysics フェーズで剛体を生成しておく必要がある。
REGISTER_LOGIC_SYSTEM(JoltBodySetupSystem, Priority::LogicStage::L03_PrePhysics);