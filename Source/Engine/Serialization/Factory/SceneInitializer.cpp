#include "SceneInitializer.h"
#include "ECS/Common/CCL_Common.h"
#include "Game/Core/AllComponents.h"
#include "Engine/Serialization/Factory/Prefab.h"
#include "Engine/Serialization/Factory/EntityFactory.h"
#include <random>
#include <vector>

#include "Game/Logics/Test/OrbitBenchmarkComponent.h"

using namespace CCL::ECS;

void SceneInitializer::SpawnPlayerTest(
    CCL::ECS::Core::World &world, ID3D11Device *device, int count)
{
    // 1. プレイヤー生成（JSONパス指定に変更）
    Prefab::SpawnPrefab(world, "Assets/Prefabs/Player/Player.json");

    // 2. ステージ生成
    Prefab::SpawnPrefab(world, "Assets/Prefabs/Stage/Stage.json");
}

void SceneInitializer::SpawnPhysicsStressTest(
    CCL::ECS::Core::World &world, ID3D11Device *device, int count)
{
    std::mt19937                          gen(12345);
    std::uniform_real_distribution<float> xzDist(-40.0f, 40.0f);
    std::uniform_real_distribution<float> yDist(10.0f, 100.0f);

    for (int i = 0; i < count; ++i) {
        // PhysicsBall -> Primitive/PrimSphere.json で代用 (物理設定がJSONにある前提)
        auto entity = Prefab::SpawnPrefab(world, "Assets/Prefabs/Primitive/PrimSphere.json");

        if (entity != CCL::ECS::InvalidEntityID) {
            if (auto *t = world.GetComponent<TransformComponent>(entity)) {
                t->position = {xzDist(gen), yDist(gen), xzDist(gen)};
            }
        }
    }

    // 床
    Prefab::SpawnPrefab(world, "Assets/Prefabs/Stage/Map_Floor.json", {0, 0, 0});
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
        Prefab::SpawnPrefab(world, "Assets/Prefabs/Primitive/PrimSphere.json", {x, 2.0f, z});
    }

    Prefab::SpawnPrefab(world, "Assets/Prefabs/Stage/Map_Floor.json", {0, 0, 0});
}

void SceneInitializer::SpawnCameraTest(
    CCL::ECS::Core::World &world, ID3D11Device *device, int count)
{
    // プレイヤー
    EntityID playerID = Prefab::SpawnPrefab(world, "Assets/Prefabs/Player.json", {0, 0, 0});

    // 追従カメラ (ID指定ではなくJSONパス)
    // CameraBrainSystemが入っていれば自動リンクされるが、念のため手動リンクのコードも残しておく例
    EntityID camID = Prefab::SpawnPrefab(world, "Assets/Prefabs/Camera/FollowCamera.json");

    // フリーカメラ
    Prefab::SpawnPrefab(world, "Assets/Prefabs/Camera/FreeCamera.json", {0, 10, -10});

    // 床
    Prefab::SpawnPrefab(world, "Assets/Prefabs/Stage/Map_Floor.json");
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
                    Prefab::SpawnPrefab(world, "Assets/Prefabs/Primitive/PrimBox.json", pos);

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
    Prefab::SpawnPrefab(world, "Assets/Prefabs/Light/LightingSetup.json");
}

void SceneInitializer::SpawnGameLightSetting(CCL::ECS::Core::World &world, ID3D11Device *device)
{
    // ゲーム用ライティング
    Prefab::SpawnPrefab(world, "Assets/Prefabs/Light/GameLighting.json");

    // ポイントライト配置テスト
    Prefab::SpawnPrefab(world, "Assets/Prefabs/Light/PointLight.json", {0, 3, 0});
    Prefab::SpawnPrefab(world, "Assets/Prefabs/Light/PointLight.json", {5, 2, 5});
}

void SceneInitializer::SpawnJoltTest(CCL::ECS::Core::World &world, ID3D11Device *device)
{
    // ──────────────────────────────────────────
    // 1. 床の生成 (Box)
    // ──────────────────────────────────────────
    const auto floorArch = ArchetypeHelper::Generate<
        TransformComponent,
        PrimitiveComponent,
        JoltRigidbodyComponent,
        JoltBoxColliderComponent
    >();

    auto floor = EntityFactory::SpawnNamed(world, "Floor_Box", floorArch);

    floor.Set(TransformComponent{ .position = {0.0f, 0.0f, 0.0f}, .rotation = {0.0f, 0.0f, 0.0f, 1.0f}, .scale = {1.0f, 1.0f, 1.0f} })
         .Set(JoltRigidbodyComponent{ .motionType = JPH::EMotionType::Static, .objectLayer = PhysicsLayers::NON_MOVING })
         .Set(JoltBoxColliderComponent{ .halfExtent = {50.0f, 1.0f, 50.0f} })
         .Set(PrimitiveComponent{ .type = PrimitiveType::Box, .size = {50.0f, 1.0f, 50.0f}, .color = {0.5f, 0.5f, 0.5f, 1.0f} });


    // ──────────────────────────────────────────
    // 2. 落下する球の生成 (Sphere)
    // ──────────────────────────────────────────
    const auto sphereArch = ArchetypeHelper::Generate<
        TransformComponent,
        PrimitiveComponent,
        JoltRigidbodyComponent,
        JoltSphereColliderComponent
    >();

    auto sphere = EntityFactory::SpawnNamed(world, "Bouncing_Sphere", sphereArch);

    sphere.Set(TransformComponent{ .position = {0.0f, 15.0f, 0.0f}, .rotation = {0.0f, 0.0f, 0.0f, 1.0f}, .scale = {1.0f, 1.0f, 1.0f} })
          .Set(JoltRigidbodyComponent{
            .motionType = JPH::EMotionType::Dynamic, 
            .objectLayer = PhysicsLayers::MOVING, 
            .restitution = 0.8f,
            .initialVelocity = {0.0f, -5.0f, 0.0f},
            .gravityFactor = 1.0f })
          .Set(JoltSphereColliderComponent{ .radius = 1.0f })
          .Set(PrimitiveComponent{ .type = PrimitiveType::Sphere, .radius = 1.0f, .color = {1.0f, 0.2f, 0.2f, 1.0f} });


    // ──────────────────────────────────────────
    // 3. テスト用プレイヤー (CharacterVirtual) の生成
    // ──────────────────────────────────────────
    const auto playerArch = ArchetypeHelper::Generate<
        TransformComponent, 
        PrimitiveComponent,
        PlayerComponent,
        JoltCharacterConfigComponent, // ★修正: パフォーマンス低下を防ぐため設計図に含める
        JoltCapsuleColliderComponent,
        ModelComponent,   
        AnimatorComponent 
    >();

    auto player = EntityFactory::SpawnNamed(world, "Test_Player", playerArch);

    // ※ ModelComponent は SetModel() という関数呼び出しが必要なため、一度変数を作ってから流し込む
    ModelComponent modelComp;
    modelComp.SetModel("Assets/Models/Player/Player.glb");

    player.Set(TransformComponent{ .position = {0.0f, 5.0f, 0.0f}, .rotation = {0.0f, 0.0f, 0.0f, 1.0f}, .scale = {1.0f, 1.0f, 1.0f} })
          .Set(PlayerComponent{ .moveSpeed = 6.0f, .turnSpeed = 10.0f })
          .Set(JoltCharacterConfigComponent{ .maxSlopeAngle = 45.0f, .maxStepHeight = 0.5f, .walkSpeed = 6.0f, .jumpSpeed = 10.0f, .characterMass = 70.0f })
          .Set(JoltCapsuleColliderComponent{ .halfHeight = 0.5f, .radius = 0.5f })
          .Set(PrimitiveComponent{ .type = PrimitiveType::Capsule, .size = {0.0f, 1.0f, 0.0f}, .radius = 0.5f, .color = {0.2f, 0.8f, 0.2f, 1.0f} })
          .Set(std::move(modelComp)) // 作っておいた ModelComponent をムーブ（無駄なコピー回避）
          .Set(AnimatorComponent{}); // 空の初期化

    // 2. カメラの生成とターゲット設定が、一気に書ける！
    auto camera = EntityFactory::SpawnPrefab(world, "Assets/Prefabs/Camera/PlayerFollowCamera.json");
    
    // player.ID() を渡してメソッドチェーンでPatchする
    camera.Patch<CameraBodyFollow>([pid = player.ID()](auto& body) { body.target = pid; })
          .Patch<CameraAimLookAt> ([pid = player.ID()](auto& aim)  { aim.target = pid; });

}

void SceneInitializer::SpawnAnimationTest(CCL::ECS::Core::World& world, ID3D11Device* device)
{
    EntityFactory::SpawnPrefab(world, "Assets/Prefabs/Camera/FreeCamera.json");
    EntityFactory::SpawnPrefab(world, "Assets/Prefabs/World/ExampleStage.json");
    //EntityFactory::SpawnPrefab(world, "Assets/Prefabs/AnimationModel.json");
    EntityFactory::SpawnPrefab(world, "Assets/Prefabs/PBRModel.json");
}


void SceneInitializer::SpawnEnemySwarmTest(CCL::ECS::Core::World &world, ID3D11Device *device, int count)
{
   
    // ──────────────────────────────────────────
    // 3. テスト用プレイヤー (CharacterVirtual) の生成
    // ──────────────────────────────────────────
    const auto playerArch = ArchetypeHelper::Generate<
        TransformComponent, 
        PrimitiveComponent,
        PlayerComponent,
        JoltCharacterConfigComponent, // ★修正: パフォーマンス低下を防ぐため設計図に含める
        JoltCapsuleColliderComponent,
        JoltRigidbodyComponent,
        ModelComponent,   
        AnimatorComponent 
    >();

    auto player = EntityFactory::SpawnNamed(world, "Test_Player", playerArch);

    // ※ ModelComponent は SetModel() という関数呼び出しが必要なため、一度変数を作ってから流し込む
    ModelComponent modelComp;
    modelComp.SetModel("Assets/Models/Player/Player.glb");

    player.Set(TransformComponent{ .position = {0.0f, 5.0f, 0.0f}, .rotation = {0.0f, 0.0f, 0.0f, 1.0f}, .scale = {1.0f, 1.0f, 1.0f} })
          .Set(PlayerComponent{ .moveSpeed = 6.0f, .turnSpeed = 10.0f })
          .Set(JoltCharacterConfigComponent{ .maxSlopeAngle = 45.0f, .maxStepHeight = 0.5f, .walkSpeed = 6.0f, .jumpSpeed = 10.0f, .characterMass = 70.0f })
          .Set(JoltCapsuleColliderComponent{ .halfHeight = 0.5f, .radius = 0.5f })
          .Set(JoltRigidbodyComponent{
            .motionType  = JPH::EMotionType::Kinematic, // ★自力で動く無敵の物体
            .objectLayer = PhysicsLayers::MOVING})
          .Set(PrimitiveComponent{ .type = PrimitiveType::Capsule, .size = {0.0f, 1.0f, 0.0f}, .radius = 0.5f, .color = {0.2f, 0.8f, 0.2f, 1.0f} })
          .Set(std::move(modelComp)) // 作っておいた ModelComponent をムーブ（無駄なコピー回避）
          .Set(AnimatorComponent{}); // 空の初期化

    // 2. カメラの生成とターゲット設定が、一気に書ける！
    auto camera = EntityFactory::SpawnPrefab(world, "Assets/Prefabs/Camera/PlayerFollowCamera.json");
    
    // player.ID() を渡してメソッドチェーンでPatchする
    camera.Patch<CameraBodyFollow>([pid = player.ID()](auto& body) { body.target = pid; })
          .Patch<CameraAimLookAt> ([pid = player.ID()](auto& aim)  { aim.target = pid; });


    // ──────────────────────────────────────────
    // 1. 床の生成 (Box)
    // ──────────────────────────────────────────
    const auto floorArch = ArchetypeHelper::Generate<
        TransformComponent,
        PrimitiveComponent,
        JoltRigidbodyComponent,
        JoltBoxColliderComponent
    >();

    auto floor = EntityFactory::SpawnNamed(world, "Floor_Box", floorArch);

    floor.Set(TransformComponent{ .position = {0.0f, 0.0f, 0.0f}, .rotation = {0.0f, 0.0f, 0.0f, 1.0f}, .scale = {1.0f, 1.0f, 1.0f} })
         .Set(JoltRigidbodyComponent{ .motionType = JPH::EMotionType::Static, .objectLayer = PhysicsLayers::NON_MOVING })
         .Set(JoltBoxColliderComponent{ .halfExtent = {50.0f, 1.0f, 50.0f} })
         .Set(PrimitiveComponent{ .type = PrimitiveType::Box, .size = {50.0f, 1.0f, 50.0f}, .color = {0.5f, 0.5f, 0.5f, 1.0f} });


    // ──────────────────────────────────────────
    // 4. 敵の大群（Swarm）の生成
    // ──────────────────────────────────────────
    const auto enemyArch = ArchetypeHelper::Generate<
        TransformComponent,
        PrimitiveComponent,         // 描画用（赤色カプセル）
        JoltRigidbodyComponent,     // 物理の性質
        JoltCapsuleColliderComponent, // 当たり判定の形
        EnemyComponent              // 追従AI用
    >();

    std::mt19937 gen(12345);
    std::uniform_real_distribution<float> xzDist(-50.0f, 50.0f); 

    // 圧倒的スピードでメモリに叩き込む
    for (int i = 0; i < count; ++i) {
        auto ref = EntityFactory::SpawnNamed(world, "SwarmEnemy", enemyArch);

        // ★修正: Transformのrotationを明示し、順番通りにする
        // ★修正: EnemyComponent の変数は moveSpeed ではなく speed
        // ★修正: Rigidbodyの restitution(反発) も明示的に書いて警告を防ぐ
        ref.Set(TransformComponent{ .position = {xzDist(gen), 2.0f, xzDist(gen)}, .rotation = {0.0f, 0.0f, 0.0f, 1.0f}, .scale = {1.0f, 1.0f, 1.0f} })
           .Set(EnemyComponent{ .speed = (4.0f + (i % 3)) }) // moveSpeed -> speed に修正
           .Set(JoltRigidbodyComponent{ 
               .motionType  = JPH::EMotionType::Dynamic, 
               .objectLayer = PhysicsLayers::MOVING,
               .restitution = 0.0f,
               .friction    = 0.0f // 敵同士が渋滞しないように摩擦ゼロ
            })
           .Set(JoltCapsuleColliderComponent{ .halfHeight = 0.5f, .radius = 0.4f })
           .Set(PrimitiveComponent{ .type = PrimitiveType::Capsule, .size = {0.0f, 1.0f, 0.0f}, .radius = 0.4f, .color = {1.0f, 0.0f, 0.0f, 1.0f} });
    }
}


void SceneInitializer::SpawnECSBenchmarkTest(
    CCL::ECS::Core::World& world, ID3D11Device* device, int count)
{
    // 1. アーキタイプの生成（圧倒的スピードでメモリを確保するため）
    const auto benchmarkArch = CCL::ECS::ArchetypeHelper::Generate<
        TransformComponent,
        //ModelComponent,         // キューブ描画用
        //MaterialComponent,         // キューブ描画用
        PrimitiveComponent,         // キューブ描画用
        OrbitBenchmarkComponent     // 軌道計算用
    >();

    //ModelComponent baseModelComp;
    //baseModelComp.SetModel("Assets/Models/Shape/Sphere.glb");
    // ※すでにResourceManager等を経由して安全にロードされ、内部の modelPtr にセットされている


    std::mt19937 gen(12345);
    std::uniform_real_distribution<float> radiusDist(5.0f, 150.0f); // 5m〜150mの範囲
    std::uniform_real_distribution<float> angleDist(0.0f, 6.28318f); // 0〜2π
    std::uniform_real_distribution<float> speedDist(0.1f, 2.0f);
    std::uniform_real_distribution<float> waveDist(0.5f, 3.0f);

    for (int i = 0; i < count; ++i) {
        auto ref = EntityFactory::SpawnNamed(world, "BenchmarkCube", benchmarkArch);

        // 視覚的に綺麗にするため、インデックスからグラデーション色を作る
        float hue = (float)i / count;
        DirectX::XMFLOAT4 color = {
            0.5f + 0.5f * std::sin(hue * 10.0f),
            0.5f + 0.5f * std::cos(hue * 10.0f),
            1.0f,
            1.0f
        };


        // コンポーネントの初期値をセット
        ref.Set(TransformComponent{ .position = {0,0,0}, .rotation = {0,0,0,1}, .scale = {0.2f, 0.2f, 0.2f} })
            .Set(PrimitiveComponent{ .type = PrimitiveType::Box, .size = {1.0f, 1.0f, 1.0f}, .color = color })
            //.Set(ModelComponent{ baseModelComp }) // ★ Primitiveではなくモデルをセット
            .Set(OrbitBenchmarkComponent{
                .angle = angleDist(gen),
                .orbitSpeed = speedDist(gen),
                .radius = radiusDist(gen),
                .waveSpeed = waveDist(gen)
                });
    }
}

#include "Game/Logics/Test/VoxelOceanComponent.h"


void SceneInitializer::SpawnVoxelOceanTest(
    CCL::ECS::Core::World& world, ID3D11Device* device, int gridSize){
    
    // 1. アーキタイプの生成（圧倒的スピードでメモリを確保）
    const auto oceanArch = CCL::ECS::ArchetypeHelper::Generate<
        TransformComponent,
        PrimitiveComponent,   // ★ ModelComponent から PrimitiveComponent に変更
        VoxelOceanComponent   // 海の波計算用
    >();

    float spacing = 1.0f; // キューブとキューブの間隔
    float offset = (gridSize * spacing) / 2.0f; // 中心を(0,0)に合わせるためのオフセット

    // 海らしい色を定義（青緑系）
    DirectX::XMFLOAT4 oceanColor = { 0.1f, 0.6f, 0.8f, 1.0f };

    // グリッド状に配置
    for (int x = 0; x < gridSize; ++x) {
        for (int z = 0; z < gridSize; ++z) {

            auto ref = EntityFactory::SpawnNamed(world, "OceanVoxel", oceanArch);

            // XとZの座標を計算
            float posX = (x * spacing) - offset;
            float posZ = (z * spacing) - offset;

            // コンポーネントの初期値をセット
            ref.Set(TransformComponent{
                    .position = {posX, 0.0f, posZ},
                    .rotation = {0,0,0,1},
                    .scale = {0.8f, 0.8f, 0.8f} // spacingより少し小さくして隙間を空けると綺麗です
                })
                // ★ PrimitiveComponentでBox(キューブ)を指定して色をセット
                .Set(PrimitiveComponent{ 
                    .type = PrimitiveType::Box, 
                    .size = {1.0f, 1.0f, 1.0f}, 
                    .color = oceanColor 
                })
                .Set(VoxelOceanComponent{});
        }
    }
}



#include <cmath>

void SceneInitializer::SpawnPhysicsAvalancheTest(
    CCL::ECS::Core::World& world, ID3D11Device* device, int count)
{
    // 1. アーキタイプの生成
    const auto staticArch = CCL::ECS::ArchetypeHelper::Generate<
        TransformComponent,
        PrimitiveComponent,
        JoltRigidbodyComponent,
        JoltBoxColliderComponent
    >();

    const auto dynamicArch = CCL::ECS::ArchetypeHelper::Generate<
        TransformComponent,
        PrimitiveComponent,
        JoltRigidbodyComponent,
        JoltBoxColliderComponent
    >();

    // -------------------------------------------------------------
    // 2. 巨大な斜面の生成（受け止める床）
    // -------------------------------------------------------------
    auto slope = EntityFactory::SpawnNamed(world, "Slope", staticArch);

    // X軸周りに20度回転させたクォータニオンを作成
    DirectX::XMVECTOR q = DirectX::XMQuaternionRotationAxis(
        DirectX::XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f),
        DirectX::XMConvertToRadians(20.0f)
    );
    DirectX::XMFLOAT4 rot;
    DirectX::XMStoreFloat4(&rot, q);

    slope.Set(TransformComponent{ .position = {0.0f, -10.0f, 30.0f}, .rotation = rot, .scale = {1.0f, 1.0f, 1.0f} })
        .Set(PrimitiveComponent{ .type = PrimitiveType::Box, .size = {120.0f, 3.0f, 120.0f}, .color = {0.3f, 0.3f, 0.3f, 1.0f} })
        .Set(JoltBoxColliderComponent{ .halfExtent = {120.0f, 3.0f, 120.0f} }) // sizeの半分
        .Set(JoltRigidbodyComponent{
            .motionType = JPH::EMotionType::Static,
            .objectLayer = PhysicsLayers::NON_MOVING,
            .friction = 0.5f
            });

    // -------------------------------------------------------------
    // 3. 上空から降り注ぐキューブ群の生成
    // -------------------------------------------------------------
    // count(例: 50,000)の立方根をとって、3Dのグリッド状に配置する
    int sideSize = static_cast<int>(std::cbrt(count));
    float spacing = 5.0f; // 1.0fのキューブに対して少し隙間を空ける
    float offset = (sideSize * spacing) / 2.0f;

    std::mt19937 gen(12345);
    std::uniform_real_distribution<float> colorDist(0.5f, 1.0f);

    int spawned = 0;
    for (int y = 0; y < sideSize && spawned < count; ++y) {
        for (int x = 0; x < sideSize && spawned < count; ++x) {
            for (int z = 0; z < sideSize && spawned < count; ++z) {

                auto cube = EntityFactory::SpawnNamed(world, "PhysicsCube", dynamicArch);

                float posX = (x * spacing) - offset;
                float posY = 160.0f + (y * spacing); // 上空に配置
                float posZ = (z * spacing) - offset;

                // ランダムな色（視覚的に個体を見分けやすくする）
                DirectX::XMFLOAT4 color = { colorDist(gen), colorDist(gen), 1.0f, 1.0f };

                cube.Set(TransformComponent{ .position = {posX, posY, posZ}, .rotation = {0,0,0,1}, .scale = {1.0f, 1.0f, 1.0f} })
                    .Set(PrimitiveComponent{ .type = PrimitiveType::Box, .size = {1.0f, 1.0f, 1.0f}, .color = color })
                    .Set(JoltBoxColliderComponent{ .halfExtent = {1.0f, 1.0f, 1.0f} })
                    .Set(JoltRigidbodyComponent{
                        .motionType = JPH::EMotionType::Dynamic,
                        .objectLayer = PhysicsLayers::MOVING,
                        .restitution = 0.2f, // 少し弾む
                        .friction = 0.3f
                        });

                spawned++;
            }
        }
    }
}