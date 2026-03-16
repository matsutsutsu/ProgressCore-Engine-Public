#include "SceneInitializer.h"
#include "ECS/Common/CCL_Common.h"
#include "Game/Core/AllComponents.h"
#include "Engine/Serialization/Factory/Prefab.h"
#include <random>
#include <vector>

using namespace CCL::ECS;

void SceneInitializer::SpawnPlayerTest(
    CCL::ECS::Core::World &world, ID3D11Device *device, int count)
{
    // 1. プレイヤー生成（JSONパス指定に変更）
    Prefab::SpawnPrefab(world, "Assets/GamePlay/Prefabs/Player/Player.json");

    // 2. ステージ生成
    Prefab::SpawnPrefab(world, "Assets/GamePlay/Prefabs/Stage/Stage.json");
}

void SceneInitializer::SpawnPhysicsStressTest(
    CCL::ECS::Core::World &world, ID3D11Device *device, int count)
{
    std::mt19937                          gen(12345);
    std::uniform_real_distribution<float> xzDist(-40.0f, 40.0f);
    std::uniform_real_distribution<float> yDist(10.0f, 100.0f);

    for (int i = 0; i < count; ++i) {
        // PhysicsBall -> Primitive/PrimSphere.json で代用 (物理設定がJSONにある前提)
        auto entity = Prefab::SpawnPrefab(world, "Assets/GamePlay/Prefabs/Primitive/PrimSphere.json");

        if (entity != CCL::ECS::InvalidEntityID) {
            if (auto *t = world.GetComponent<TransformComponent>(entity)) {
                t->position = {xzDist(gen), yDist(gen), xzDist(gen)};
            }
        }
    }

    // 床
    Prefab::SpawnPrefab(world, "Assets/GamePlay/Prefabs/Stage/Map_Floor.json", {0, 0, 0});
}

void SceneInitializer::SpawnGridLayoutTest(
    CCL::ECS::Core::World &world, ID3D11Device *device, int count)
{
    // グリッド状に並べる
    int   side    = (int)std::ceil(std::sqrt(count));
    float spacing = 2.0f;
    float offset  = -((side - 1) * spacing) / 2.0f;

    for (int i = 0; i < count; ++i) {
        int r = i / side;
        int c = i % side;

        float x = offset + c * spacing;
        float z = offset + r * spacing;

        // RenderModel -> プリミティブの球体などで代用
        Prefab::SpawnPrefab(world, "Assets/GamePlay/Prefabs/Primitive/PrimSphere.json", {x, 2.0f, z});
    }

    Prefab::SpawnPrefab(world, "Assets/GamePlay/Prefabs/Stage/Map_Floor.json", {0, 0, 0});
}

void SceneInitializer::SpawnCameraTest(
    CCL::ECS::Core::World &world, ID3D11Device *device, int count)
{
    // プレイヤー
    EntityID playerID = Prefab::SpawnPrefab(world, "Assets/GamePlay/Prefabs/Player.json", {0, 0, 0});

    // 追従カメラ (ID指定ではなくJSONパス)
    // CameraBrainSystemが入っていれば自動リンクされるが、念のため手動リンクのコードも残しておく例
    EntityID camID = Prefab::SpawnPrefab(world, "Assets/GamePlay/Prefabs/Camera/FollowCamera.json");

    // フリーカメラ
    Prefab::SpawnPrefab(world, "Assets/GamePlay/Prefabs/Camera/FreeCamera.json", {0, 10, -10});

    // 床
    Prefab::SpawnPrefab(world, "Assets/GamePlay/Prefabs/Stage/Map_Floor.json");
}

// ★追加: Transform負荷テストの実装
void SceneInitializer::SpawnTransformLoadTest(
    CCL::ECS::Core::World &world, ID3D11Device *device, int count)
{
    // 立方体状に配置するための計算
    // 例: 10000個なら 22x22x22 くらいのブロックを作る
    int   dim     = (int)std::ceil(std::cbrt(count));
    float spacing = 1.5f; // 間隔

    int spawnedCount = 0;

    for (int x = 0; x < dim; ++x) {
        for (int y = 0; y < dim; ++y) {
            for (int z = 0; z < dim; ++z) {
                if (spawnedCount >= count) break;

                // 単純なボックスを生成
                DirectX::XMFLOAT3 pos = {(x - dim / 2.0f) * spacing,
                    (y - dim / 2.0f) * spacing,
                    (z - dim / 2.0f) * spacing};

                EntityID entity =
                    Prefab::SpawnPrefab(world, "Assets/GamePlay/Prefabs/Primitive/PrimBox.json", pos);

                if (entity != CCL::ECS::InvalidEntityID) {
                    // 1.
                    // 純粋なTransform負荷を見るため、物理演算(RigidBody/Collider)が付いていれば外す
                    // （もしPrefabに物理が付いていないならこの処理は無視されますが、念のため）
                    // ★追加検証: 描画負荷を消してみる
                    // これで「純粋な計算速度」だけでFPSが出るようになります
   
                    //// PrimitiveComponentを使っている場合
                    //if (world.HasComponent<PrimitiveComponent>(entity)) {
                    //    world.RequestRemoveComponent<PrimitiveComponent>(entity);
                    //}

                    // 2. 確実に毎フレーム更新させる
                    // TransformUpdateSystemは !isStatic なら動かなくても行列計算を行う仕様を利用
                    world.PatchComponent<TransformComponent>(entity, [](TransformComponent &t) {
                        t.isStatic = false;

                        // 少し回転させておくと、計算結果が変わるのでデバッグしやすい
                        // (クォータニオン計算負荷も乗るかも)
                        // t.rotation = ...;
                    });
                }

                spawnedCount++;
            }
        }
    }
}

void SceneInitializer::SpawnLightStartSet(CCL::ECS::Core::World &world, ID3D11Device *device)
{
    // ライティングセットアップ
    Prefab::SpawnPrefab(world, "Assets/GamePlay/Prefabs/Light/LightingSetup.json");
}

void SceneInitializer::SpawnGameLightSetting(CCL::ECS::Core::World &world, ID3D11Device *device)
{
    // ゲーム用ライティング
    Prefab::SpawnPrefab(world, "Assets/GamePlay/Prefabs/Light/GameLighting.json");

    // ポイントライト配置テスト
    Prefab::SpawnPrefab(world, "Assets/GamePlay/Prefabs/Light/PointLight.json", {0, 3, 0});
    Prefab::SpawnPrefab(world, "Assets/GamePlay/Prefabs/Light/PointLight.json", {5, 2, 5});
}

void SceneInitializer::SpawnJoltTest(CCL::ECS::Core::World &world, ID3D11Device *device)
{
    // ──────────────────────────────────────────
    // 1. 床の生成 (Box)
    // ──────────────────────────────────────────
    auto floorArchetype = ArchetypeHelper::Generate<TransformComponent,
        PrimitiveComponent,
        JoltRigidbodyComponent,  // 物理の性質（重さ、摩擦など）
        JoltBoxColliderComponent // 物理の形（箱）
        >();
    auto floorEntity    = world.RequestSpawnEntity(floorArchetype);

    TransformComponent floorTrans;
    floorTrans.position = {0.0f, -2.0f, 0.0f};
    floorTrans.rotation = {0.0f, 0.0f, 0.0f, 1.0f};
    floorTrans.scale    = {1.0f, 1.0f, 1.0f};
    world.AddComponent<TransformComponent>(floorEntity, floorTrans);

    // ★追加: 物理の「性質」を設定
    JoltRigidbodyComponent floorRigidbody;
    floorRigidbody.motionType  = JPH::EMotionType::Static; // 動かない
    floorRigidbody.objectLayer = PhysicsLayers::NON_MOVING;
    world.AddComponent<JoltRigidbodyComponent>(floorEntity, floorRigidbody);

    // ★追加: 物理の「形」を設定
    JoltBoxColliderComponent floorCollider;
    floorCollider.halfExtent = {50.0f, 1.0f, 50.0f}; // 半分のサイズ
    world.AddComponent<JoltBoxColliderComponent>(floorEntity, floorCollider);

    // 描画の設定
    PrimitiveComponent floorPrim;
    floorPrim.type  = PrimitiveType::Box;
    floorPrim.size  = {50.0f, 1.0f, 50.0f}; // 前回確認した通り、描画も2倍せずに合わせる
    floorPrim.color = {0.5f, 0.5f, 0.5f, 1.0f};
    world.AddComponent<PrimitiveComponent>(floorEntity, floorPrim);

    // ──────────────────────────────────────────
    // 2. 落下する球の生成 (Sphere)
    // ──────────────────────────────────────────
    auto sphereArchetype = ArchetypeHelper::Generate<TransformComponent,
        PrimitiveComponent,
        JoltRigidbodyComponent,     // 物理の性質
        JoltSphereColliderComponent // 物理の形（球）
        >();
    auto sphereEntity    = world.RequestSpawnEntity(sphereArchetype);

    TransformComponent sphereTrans;
    sphereTrans.position = {0.0f, 15.0f, 0.0f};
    sphereTrans.rotation = {0.0f, 0.0f, 0.0f, 1.0f};
    sphereTrans.scale    = {1.0f, 1.0f, 1.0f};
    world.AddComponent<TransformComponent>(sphereEntity, sphereTrans);

    // ★追加: 物理の「性質」を設定
    JoltRigidbodyComponent sphereRigidbody;
    sphereRigidbody.motionType  = JPH::EMotionType::Dynamic; // 動く（重力に従う）
    sphereRigidbody.objectLayer = PhysicsLayers::MOVING;
    sphereRigidbody.restitution = 0.8f; // よく跳ねるように設定
    world.AddComponent<JoltRigidbodyComponent>(sphereEntity, sphereRigidbody);

    // ★追加: 物理の「形」を設定
    JoltSphereColliderComponent sphereCollider;
    sphereCollider.radius = 1.0f;
    world.AddComponent<JoltSphereColliderComponent>(sphereEntity, sphereCollider);

    // 描画の設定
    PrimitiveComponent spherePrim;
    spherePrim.type   = PrimitiveType::Sphere;
    spherePrim.radius = 1.0f;
    spherePrim.color  = {1.0f, 0.2f, 0.2f, 1.0f};
    world.AddComponent<PrimitiveComponent>(sphereEntity, spherePrim);
}