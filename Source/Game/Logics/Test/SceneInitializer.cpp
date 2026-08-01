#include "SceneInitializer.h"

#include "Engine/ECS/Common/CCL_Types.h"
#include "Engine/Serialization/Factory/Prefab.h"
#include "Engine/Serialization/Factory/EntityFactory.h"
#include "Engine/Platform/Core/Logger.h"
#include "Engine/Time/TimeState.h"
#include "Engine/Camera/Components/CameraBodyActionTPSComponent.h"

#include "Game/Core/AllComponents.h"
#include "Game/Logics/Test/OrbitBenchmarkComponent.h"
#include "Game/Logics/Test/VoxelOceanComponent.h"
#include "Game/Logics/Test/LissajousOrbitComponent.h"
#include "Game/Logics/Test/TornadoVortexComponent.h"
#include "Game/Logics/Test/VectorFlowComponent.h"
#include "Game/Logics/Test/BoidComponent.h"

#include <cmath>
#include <random>
#include <vector>

using namespace CCL::ECS;


void SceneInitializer::SpawnCameraTest(
    CCL::ECS::World &world, ID3D11Device *device, int count)
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

void SceneInitializer::SpawnLightStartSet(CCL::ECS::World &world, ID3D11Device *device)
{
    // ライティングセットアップ
    Prefab::SpawnPrefab(world, "Assets/Prefabs/Light/LightingSetup.json");
}



void SceneInitializer::SpawnAnimationTest(CCL::ECS::World& world, ID3D11Device* device)
{
    EntityFactory::SpawnPrefab(world, "Assets/Prefabs/Camera/FreeCamera.json");
    EntityFactory::SpawnPrefab(world, "Assets/Prefabs/World/ExampleStage.json");
    EntityFactory::SpawnPrefab(world, "Assets/Prefabs/AnimationModel.json");
   
}

void SceneInitializer::SpawnGameTest(CCL::ECS::World &world, ID3D11Device *device)
{
    EntityFactory::SpawnPrefab(world, "Assets/Prefabs/Camera/FreeCamera.json");
    EntityFactory::SpawnPrefab(world, "Assets/Prefabs/World/JoltExampleStage.json");
    EntityFactory::SpawnPrefab(world, "Assets/Prefabs/Enemy/Muriel.json");
    EntityFactory::SpawnPrefab(world, "Assets/Prefabs/Particles/StageParticle.json");

    auto player = EntityFactory::SpawnPrefab(world, "Assets/Prefabs/Player/Player.json");
    auto camera = EntityFactory::SpawnPrefab(world, "Assets/Prefabs/Player/PlayerFollowCamera.json");

    // player.ID() を渡してメソッドチェーンでPatchする
    camera.Patch<CameraBodyTPSComponent>([pid = player.ID()](auto &body) { body.target = pid; })
        .Patch<CameraAimLookAt>([pid = player.ID()](auto &aim) { aim.target = pid; });
}

void SceneInitializer::SpawnShaderTest(CCL::ECS::World &world, ID3D11Device *device)
{
    EntityFactory::SpawnPrefab(world, "Assets/Prefabs/Camera/FreeCamera.json");
    EntityFactory::SpawnPrefab(world, "Assets/Prefabs/Sponza.json");
    EntityFactory::SpawnPrefab(world, "Assets/Prefabs/PBRModel.json");
}



// ============================================================================
// 指定した数のモデルを並べるテスト環境
// ============================================================================
void SceneInitializer::SpawnModelGridTest(
    CCL::ECS::World& world, ID3D11Device* device, int count)
{
    // 1. 空間を見るためのフリーカメラを配置
    EntityFactory::SpawnPrefab(world, "Assets/Prefabs/Camera/FreeCamera.json");

    // 2. アーキタイプ（設計図）の生成
    // メモリを連続して確保するため、アーキタイプを事前に定義します
    const auto modelArch = CCL::ECS::ArchetypeHelper::Generate<
        TransformComponent,
        ModelComponent,
        MaterialComponent
    >();

    // 3. モデルデータのキャッシュ（★アーキテクトの極意）
    // ループの中で毎回 SetModel を呼ぶと、数万回もパスの検索とロードが走ってしまいます。
    // そのため、あらかじめ1つだけ「マスターとなる ModelComponent」を作り、使い回します。
    ModelComponent baseModelComp;
    baseModelComp.SetModel("Assets/Models/Test/DamagedHelmet.glb"); // ※お手持ちのモデルのパスに変更してください

    // 4. グリッド配置の計算
    // count の平方根を切り上げて、XとZの1辺あたりの配置数を決めます
    int sideSize = static_cast<int>(std::ceil(std::sqrt(count)));

    float spacing = 2.0f; // モデル同士の間隔（モデルの大きさに合わせて調整してください）
    float offset = (sideSize * spacing) / 2.0f; // 全体の中心を (0,0) に持ってくるためのオフセット

    // 5. 大量生成ループ
    int spawned = 0;
    for (int z = 0; z < sideSize && spawned < count; ++z) {
        for (int x = 0; x < sideSize && spawned < count; ++x) {

            // アーキタイプを使って最速でエンティティを生成
            auto ref = EntityFactory::SpawnNamed(world, "GridModel", modelArch);

            // 座標の計算
            float posX = (x * spacing) - offset;
            float posZ = (z * spacing) - offset;

            // コンポーネントの流し込み
            ref.Set(TransformComponent{
                    .position = {posX, 0.0f, posZ},
                    .rotation = {0.0f, 0.0f, 0.0f, 1.0f},
                    .scale = {1.0f, 1.0f, 1.0f}
                })
                .Set(ModelComponent{ baseModelComp }); // キャッシュしたモデルをコピー

            spawned++;
        }
    }
}


void SceneInitializer::SpawnECSBenchmarkTest(CCL::ECS::World &world, ID3D11Device *device, int count)
{
    std::mt19937                          gen(42);
    std::uniform_real_distribution<float> distAngle(0.0f, DirectX::XM_2PI);
    std::uniform_real_distribution<float> distRadius(5.0f, 100.0f);
    std::uniform_real_distribution<float> distColor(0.5f, 1.0f);

    auto arch = CCL::ECS::ArchetypeHelper::Generate<TransformComponent, PrimitiveComponent,TimeStateComponent, OrbitBenchmarkComponent>();

    for (int i = 0; i < count; ++i)
    {
        auto e = EntityFactory::SpawnNamed(world, "OrbitParticle", arch);

        e.Set(TransformComponent{.rotation = {0, 0, 0, 1}, .scale = {0.5f, 0.5f, 0.5f}});

        // 宇宙の星屑をイメージした青紫系のカラー
        e.Set(PrimitiveComponent{.type = PrimitiveType::Sphere, .color = {distColor(gen) * 0.5f, distColor(gen) * 0.5f, 1.0f, 1.0f}});

        // ★修正: 不変データ（初期角度と半径）だけをセットする
        e.Set(OrbitBenchmarkComponent{
            .phaseOffset = distAngle(gen),
            .radius      = distRadius(gen)});
    }
}

void SceneInitializer::SpawnVoxelOceanTest(
    CCL::ECS::World& world, ID3D11Device* device, int gridSize){
    

    Prefab::SpawnPrefab(world, "Assets/Prefabs/Camera/FreeCamera.json");
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



void SceneInitializer::SpawnPhysicsAvalancheTest(
    CCL::ECS::World& world, ID3D11Device* device, int count)
{
    // 1. アーキタイプの生成
    const auto staticArch = CCL::ECS::ArchetypeHelper::Generate<
        TransformComponent,
        PrimitiveComponent,
        TimeStateComponent,
        JoltRigidbodyComponent,
        JoltBoxColliderComponent
    >();

    const auto dynamicArch = CCL::ECS::ArchetypeHelper::Generate<
        TransformComponent,
        PrimitiveComponent,
        TimeStateComponent,
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
        DirectX::XMConvertToRadians(0.0f)
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
    float spacing = 2.0f; // 1.0fのキューブに対して少し隙間を空ける
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
                        .objectLayer = PhysicsLayers::PLAYER,
                        .restitution = 0.2f, // 少し弾む
                        .friction = 0.3f
                        });

                spawned++;
            }
        }
    }
}


// ※PrimitiveComponent などのインクルードは適宜行ってください

void SceneInitializer::SpawnJoltAvalancheTest(CCL::ECS::World &world, ID3D11Device *device, int count)
{
    // ====================================================================
    // 1. 巨大な床（受け皿）の生成 (Static)
    // ====================================================================
    auto staticBoxArch = CCL::ECS::ArchetypeHelper::Generate<TransformComponent, PrimitiveComponent, JoltRigidbodyComponent, JoltBoxColliderComponent>();

    auto floor = EntityFactory::SpawnNamed(world, "Floor", staticBoxArch);
    floor.Set(TransformComponent{
        .position = {0, -10.0f, 0}, .rotation = {0, 0, 0, 1}, .scale = {200.0f, 10.0f, 200.0f}, .isStatic = true, .isDirty = true});
    floor.Set(PrimitiveComponent{.type = PrimitiveType::Box, .color = {0.2f, 0.2f, 0.25f, 1.0f}});
    floor.Set(JoltBoxColliderComponent{.halfExtent = {100.0f, 5.0f, 100.0f}});

    // ※ エンジンの仕様に合わせて Static を指定してください
    // 例: JoltMotionType::Static など
    floor.Set(JoltRigidbodyComponent{
        .motionType  = JPH::EMotionType::Static,
        .objectLayer = PhysicsLayers::NON_MOVING,
    });

    // ====================================================================
    // 2. 中央の巨大な障害物（四方八方に弾けさせるためのコア）
    // ====================================================================
    auto staticSphereArch = CCL::ECS::ArchetypeHelper::Generate<TransformComponent, PrimitiveComponent, JoltRigidbodyComponent, JoltSphereColliderComponent>();

    auto obstacle = EntityFactory::SpawnNamed(world, "CenterObstacle", staticSphereArch);
    obstacle.Set(TransformComponent{
        .position = {0, 15.0f, 0}, .rotation = {0, 0, 0, 1}, .scale = {30.0f, 30.0f, 30.0f}, .isStatic = true, .isDirty = true});
    obstacle.Set(PrimitiveComponent{.type = PrimitiveType::Sphere, .color = {0.8f, 0.2f, 0.2f, 1.0f}}); // 赤色
    obstacle.Set(JoltSphereColliderComponent{.radius = 15.0f});
    obstacle.Set(JoltRigidbodyComponent{
        .motionType = JPH::EMotionType::Static,
        .objectLayer = PhysicsLayers::NON_MOVING,
    });

    // ====================================================================
    // 3. 空中から降り注ぐ大量の剛体の生成 (Dynamic)
    // ====================================================================
    std::mt19937                          gen(42);
    std::uniform_real_distribution<float> posDist(-40.0f, 40.0f);    // スポーン範囲(XZ)
    std::uniform_real_distribution<float> heightDist(50.0f, 250.0f); // スポーン高度(Y) かなり上空まで
    std::uniform_real_distribution<float> rotDist(0.0f, DirectX::XM_2PI);
    std::uniform_real_distribution<float> colorDist(0.3f, 1.0f);
    std::uniform_int_distribution<int>    typeDist(0, 1); // 0: Box, 1: Sphere

    auto dynamicBoxArch    = CCL::ECS::ArchetypeHelper::Generate<TransformComponent, PrimitiveComponent,TimeStateComponent, JoltRigidbodyComponent, JoltBoxColliderComponent>();
    auto dynamicSphereArch = CCL::ECS::ArchetypeHelper::Generate<TransformComponent, PrimitiveComponent,TimeStateComponent, JoltRigidbodyComponent, JoltSphereColliderComponent>();

    for (int i = 0; i < count; ++i)
    {
        float x = posDist(gen);
        float y = heightDist(gen);
        float z = posDist(gen);

        // 個体ごとにランダムな色
        DirectX::XMFLOAT4 color = {colorDist(gen), colorDist(gen), colorDist(gen), 1.0f};
        // ランダムな初期回転
        DirectX::SimpleMath::Quaternion rot = DirectX::SimpleMath::Quaternion::CreateFromYawPitchRoll(rotDist(gen), rotDist(gen), rotDist(gen));

        if (typeDist(gen) == 0)
        {
            // --- 動的な Box ---
            auto e = EntityFactory::SpawnNamed(world, "DebrisBox", dynamicBoxArch);
            e.Set(TransformComponent{
                .position = {x, y, z}, .rotation = {rot.x, rot.y, rot.z, rot.w}, .scale = {1.0f, 1.0f, 1.0f}, .isStatic = false, .isDirty = true});
            e.Set(PrimitiveComponent{.type = PrimitiveType::Box, .color = color});
            e.Set(JoltBoxColliderComponent{.halfExtent = {0.5f, 0.5f, 0.5f}});
            e.Set(JoltRigidbodyComponent{/* MotionType = Dynamic */});
        }
        else
        {
            // --- 動的な Sphere ---
            auto e = EntityFactory::SpawnNamed(world, "DebrisSphere", dynamicSphereArch);
            e.Set(TransformComponent{
                .position = {x, y, z}, .rotation = {rot.x, rot.y, rot.z, rot.w}, .scale = {1.0f, 1.0f, 1.0f}, .isStatic = false, .isDirty = true});
            e.Set(PrimitiveComponent{.type = PrimitiveType::Sphere, .color = color});
            e.Set(JoltSphereColliderComponent{.radius = 0.5f});
            e.Set(JoltRigidbodyComponent{/* MotionType = Dynamic */});
        }
    }
}

void SceneInitializer::SpawnBoidsTest(CCL::ECS::World &world, ID3D11Device *device, int count)
{
    std::mt19937                          gen(1337);
    std::uniform_real_distribution<float> distPos(-20.0f, 20.0f);
    std::uniform_real_distribution<float> distVel(-5.0f, 5.0f);
    std::uniform_real_distribution<float> distColor(0.5f, 1.0f);

    auto arch = CCL::ECS::ArchetypeHelper::Generate<TransformComponent, PrimitiveComponent, BoidComponent>();

    for (int i = 0; i < count; ++i)
    {
        auto e = EntityFactory::SpawnNamed(world, "Boid", arch);

        float x = distPos(gen);
        float y = 20.0f + distPos(gen);
        float z = distPos(gen);

        // 進行方向がわかりやすいように、Z軸(前)に細長いConeやCapsuleを使用する
        e.Set(TransformComponent{
            .position = {x, y, z}, .rotation = {0, 0, 0, 1}, .scale = {0.3f, 0.3f, 0.8f}, .isStatic = false, .isDirty = true});

        // 色を少しずつバラけさせる
        DirectX::XMFLOAT4 color = {0.2f, distColor(gen), 1.0f, 1.0f}; // 青緑系
        e.Set(PrimitiveComponent{.type = PrimitiveType::Cone, .color = color});

        e.Set(BoidComponent{
            .velocity       = {distVel(gen), distVel(gen), distVel(gen)}});
    }
}

void SceneInitializer::SpawnLissajousTest(CCL::ECS::World &world, ID3D11Device *device, int count)
{
    std::mt19937 gen(1001);
    std::uniform_real_distribution<float> distPhase(0.0f, DirectX::XM_2PI);
    std::uniform_real_distribution<float> distAmp(5.0f, 20.0f);
    std::uniform_real_distribution<float> distFreq(1.0f, 5.0f);

    auto arch = CCL::ECS::ArchetypeHelper::Generate<TransformComponent, PrimitiveComponent, LissajousOrbitComponent>();

    for (int i = 0; i < count; ++i) {
        auto e = EntityFactory::SpawnNamed(world, "LissajousNode", arch);
        e.Set(TransformComponent{ .position = {0, 10, 0}, .scale = {0.2f, 0.2f, 0.2f} });
        e.Set(PrimitiveComponent{ .type = PrimitiveType::Sphere, .color = {0.0f, 1.0f, 1.0f, 1.0f} }); // シアン系

        e.Set(LissajousOrbitComponent{
            .amplitude = {distAmp(gen), distAmp(gen) * 0.5f, distAmp(gen)},
            .frequency = {distFreq(gen), distFreq(gen) * 1.5f, distFreq(gen) * 0.8f},
            .phase = {distPhase(gen), distPhase(gen), distPhase(gen)},
            .timeAcc = 0.0f,
            .speed = 0.5f,
            .centerPos = {0, 15, 0}
            });
    }
}

void SceneInitializer::SpawnTornadoTest(CCL::ECS::World& world, ID3D11Device* device, int count) {
    std::mt19937 gen(2002);
    std::uniform_real_distribution<float> distRadius(5.0f, 20.0f);
    std::uniform_real_distribution<float> distAngle(0.0f, DirectX::XM_2PI);
    std::uniform_real_distribution<float> distHeight(0.0f, 50.0f);

    auto arch = CCL::ECS::ArchetypeHelper::Generate<TransformComponent, PrimitiveComponent, TornadoVortexComponent>();

    for (int i = 0; i < count; ++i) {
        auto e = EntityFactory::SpawnNamed(world, "TornadoDebris", arch);
        e.Set(TransformComponent{ .scale = {1.0f, 1.0f, 1.0f} });
        e.Set(PrimitiveComponent{ .type = PrimitiveType::Box, .color = {0.8f, 0.2f, 0.8f, 1.0f} }); // 紫系

        e.Set(TornadoVortexComponent{
            .radius = distRadius(gen),
            .angle = distAngle(gen),
            .height = distHeight(gen),
            .shrinkSpeed = 2.0f,
            .rotationSpeed = 3.0f + distRadius(gen) * 0.1f, // 外側ほど速い等の調整
            .riseSpeed = 5.0f + distRadius(gen) * 0.2f,
            .maxRadius = 25.0f,
            .centerPos = {20, 0, 20}
            });
    }
}

void SceneInitializer::SpawnVectorFlowTest(CCL::ECS::World& world, ID3D11Device* device, int count) {
    std::mt19937 gen(3003);
    std::uniform_real_distribution<float> distPos(-30.0f, 30.0f);

    auto arch = CCL::ECS::ArchetypeHelper::Generate<TransformComponent, PrimitiveComponent, VectorFlowComponent>();

    for (int i = 0; i < count; ++i) {
        auto e = EntityFactory::SpawnNamed(world, "FlowParticle", arch);
        e.Set(TransformComponent{
            .position = {distPos(gen), distPos(gen) + 30.0f, distPos(gen)},
            .scale = {0.1f, 0.1f, 0.5f} // 進行方向へ長くする(疑似モーションブラー)
            });
        e.Set(PrimitiveComponent{ .type = PrimitiveType::Capsule, .color = {0.2f, 1.0f, 0.2f, 0.5f} }); // 半透明の緑

        e.Set(VectorFlowComponent{
            .timeAcc = distPos(gen), // 個体ごとの時間ズレ
            .frequency = 0.1f,
            .amplitude = 15.0f,
            .baseSpeed = 1.5f
            });
    }
}
