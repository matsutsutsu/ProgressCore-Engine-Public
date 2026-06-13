#include "SceneInitializer.h"
#include "Engine/ECS/Common/CCL_Common.h"
#include "Game/Core/AllComponents.h"
#include "Engine/Serialization/Factory/Prefab.h"
#include "Engine/Serialization/Factory/EntityFactory.h"
#include "Engine/Platform/Core/Logger.h"
#include <random>
#include <vector>

#include "Game/Logics/Test/OrbitBenchmarkComponent.h"
#include "Engine/Timer/TimeState.h"

using namespace CCL::ECS;

// ダンジョン生成ロジック（配置先に合わせてパス調整）
#include "Game/Logics/LevelGen/DungeonGenerator.h"
#include "Game/Logics/LevelGen/DungeonMeshBatcher.h"

#include <DirectXMath.h>

using namespace CCL::ECS;
using namespace RuneForge::Dungeon;


namespace
{
    // 床・壁マップチップモデルのパス（プロジェクトに合わせて調整）
    constexpr const char* kFloorModelPath = "Assets/Models/MapChip/Floor.glb";
    constexpr const char* kWallModelPath = "Assets/Models/MapChip/Wall.glb";

    // 1チャンクの一辺（セル数）。前回チームは 20。16〜20 が目安。
    constexpr int kChunkSize = 16;
}




void SceneInitializer::SpawnDungeonTest(
    CCL::ECS::Core::World& world, ID3D11Device* device,
    uint32_t seed, int floorIndex)
{
    // device 未指定なら Graphics から取得してもよいが、呼び出し側で渡す前提。
    if (!device)
    {
        CCL_LOG_WARN(LogCategory::Core, "[Dungeon] device is null; abort spawn.");
        return;
    }

    // ----------------------------------------------------------------
    // 1. 生成（決定的）＋ ASCII デバッグ出力
    // ----------------------------------------------------------------
    DungeonGenerateResult result = DungeonGenerator::Generate(seed, floorIndex);
    DungeonGenerator::DebugPrintGrid(result);

    CCL_LOG_INFO(LogCategory::Core,
        "[Dungeon] Building floor %d seed=%u grid=%dx%d (chunk=%d)",
        floorIndex, result.usedSeed, result.width, result.depth, kChunkSize);

    // ----------------------------------------------------------------
    // 2. マップチップモデルをロード（チャンク間で共有）
    //    CreateFromData に渡す baseMaterial もこのモデルから取る。
    // ----------------------------------------------------------------
    auto floorModel = std::make_shared<Model>(device, kFloorModelPath);
    auto wallModel = std::make_shared<Model>(device, kWallModelPath);

    // ---- 診断: モデルがちゃんとメッシュを持っているか確認 ----
    auto countVerts = [](const std::shared_ptr<Model>& m) -> size_t {
        if (!m) return 0;
        const ModelResource* res = m->GetResource();
        if (!res) return 0;
        size_t v = 0;
        for (const auto& mesh : res->GetMeshes()) v += mesh.vertices.size();
        return v;
        };
    size_t floorVerts = countVerts(floorModel);
    size_t wallVerts = countVerts(wallModel);
    CCL_LOG_INFO(LogCategory::Core,
        "[Dungeon] chip models loaded: floor='%s' verts=%zu | wall='%s' verts=%zu",
        kFloorModelPath, floorVerts, kWallModelPath, wallVerts);

    if (floorVerts == 0 && wallVerts == 0) {
        CCL_LOG_ERROR(LogCategory::Core,
            "[Dungeon] ABORT: both chip models have 0 vertices. "
            "Check that the .glb paths exist relative to the working directory.");
        return;
    }

    // baseMaterial（無ければ CreateFromData 側でデフォルトPBRが入る）
    std::shared_ptr<MaterialData> floorMat, wallMat;
    if (!floorModel->GetMaterials().empty()) floorMat = floorModel->GetMaterials()[0].data;
    if (!wallModel->GetMaterials().empty())  wallMat = wallModel->GetMaterials()[0].data;
    // 床・壁が混在する1メッシュなので、代表マテリアルは壁を優先（無ければ床）
    std::shared_ptr<MaterialData> chunkMat = wallMat ? wallMat : floorMat;

    // ----------------------------------------------------------------
    // 3. チャンクループ：床＋壁を1バッチ→1Model→1エンティティ
    // ----------------------------------------------------------------
    const auto chunkArch = ArchetypeHelper::Generate<
        TransformComponent,
        ModelComponent
    >();

    int chunkCount = 0;
    long totalVerts = 0, totalTris = 0;
    EntityID firstChunkId{};            // 診断用
    ModelInstanceHandle firstHandle{};  // 診断用

    for (int cj = 0; cj < result.depth; cj += kChunkSize)
    {
        for (int ci = 0; ci < result.width; ci += kChunkSize)
        {
            // 床＋壁を1メッシュに結合
            auto batch = DungeonMeshBatcher::BuildChunk(
                result, ci, cj, kChunkSize, kChunkSize, floorModel, wallModel);

            if (batch.vertices.empty()) continue;

            // ランタイムModel生成
            auto chunkModel = Model::CreateFromData(
                device, batch.vertices, batch.indices, chunkMat);
            if (!chunkModel) continue;

            // ★★ 診断: 全チャンクの「リソース同一性」と「頂点座標」を出す。
            //    全部同じ形に見える原因が、リソース共有なのか頂点座標なのかを切り分ける。
            {
                const ModelResource* res = chunkModel->GetResource();
                const auto& mm = chunkModel->GetMeshes();
                const void* dataPtr = (!mm.empty()) ? (const void*)mm[0].data : nullptr;
                // バッチ先頭頂点の座標（チャンクごとに違うはず）
                float vx = batch.vertices.empty() ? -999 : batch.vertices.front().position.x;
                float vz = batch.vertices.empty() ? -999 : batch.vertices.front().position.z;
                CCL_LOG_INFO(LogCategory::Core,
                    "[Dungeon][DIAG] chunk(%d,%d): ModelPtr=%p Resource=%p mesh.data=%p "
                    "modelMeshes=%zu firstVert=(%.1f,%.1f)",
                    ci, cj, (void*)chunkModel.get(), (const void*)res, dataPtr,
                    mm.size(), vx, vz);
            }

            // プール登録してハンドル化
            ModelInstanceHandle handle =
                ResourceManager::Instance().RegisterModelInstance(chunkModel);
            if (!handle.IsValid()) continue;

            // ★ 診断: 登録直後のハンドルと、プールから引けるか
            if (chunkCount == 0) {
                Model* check = ResourceManager::Instance().GetModelInstance(handle);
                CCL_LOG_INFO(LogCategory::Core,
                    "[Dungeon][DIAG] after Register: handle.index=%u gen=%u valid=%d "
                    "GetModelInstance=%p  meshesViaPool=%zu",
                    handle.index, handle.generation, handle.IsValid(),
                    (void*)check, check ? check->GetMeshes().size() : 0);
            }

            // エンティティ生成（Transform はワールド原点。頂点が既にワールド座標）
            auto e = EntityFactory::SpawnNamed(world, "DungeonChunk", chunkArch);

            TransformComponent trans{};
            trans.position = { batch.originX, 0.0f, batch.originZ }; // ★ チャンクのワールド原点
            trans.rotation = { 0.0f, 0.0f, 0.0f, 1.0f };
            trans.scale = { 1.0f, 1.0f, 1.0f };
            trans.isStatic = true;  // 動かないので毎フレーム更新不要
            trans.isDirty = true;  // 生成直後に1回 worldMatrix を計算させる

            ModelComponent mc{};
            mc.SetRuntimeModel(handle); // ★ ランタイム生成ハンドルを安全に差し込む

            // ★ 診断: Set する直前の mc のハンドル
            if (chunkCount == 0) {
                CCL_LOG_INFO(LogCategory::Core,
                    "[Dungeon][DIAG] before Set: mc.modelHandle.index=%u valid=%d "
                    "isRuntime=%d ownsHandle=%d",
                    mc.modelHandle.index, mc.modelHandle.IsValid(),
                    mc.isRuntime, mc.ownsHandle);
            }

            EntityID chunkId = e; // 暗黙変換で EntityID を保存
            e.Set(std::move(trans))
                .Set(std::move(mc));

            if (chunkCount == 0) { firstChunkId = chunkId; firstHandle = handle; }

            ++chunkCount;
            totalVerts += (long)batch.vertices.size();
            totalTris += (long)batch.indices.size() / 3;
        }
    }

    CCL_LOG_INFO(LogCategory::Core,
        "[Dungeon] Done. chunks=%d verts=%ld tris=%ld  Start=(%d,%d) Portal=(%d,%d)",
        chunkCount, totalVerts, totalTris,
        result.startCell.i, result.startCell.j,
        result.portalCell.i, result.portalCell.j);

    // ★ 診断: 構造変更を今すぐ適用し、実チャンクに入った ModelComponent を読み戻す。
    //    ここで handle が生きていれば SpawnDungeonTest 側は正しく、描画/その後の
    //    コピーで失われているなら ModelComponent のライフサイクル側が原因と分かる。
    world.ScrutinyAndApply();

    if (chunkCount > 0) {
        ModelComponent* applied = world.GetComponent<ModelComponent>(firstChunkId);
        if (!applied) {
            CCL_LOG_ERROR(LogCategory::Core,
                "[Dungeon][DIAG] POST-APPLY: GetComponent<ModelComponent> returned null "
                "(entity %u has no ModelComponent in its archetype?)", firstChunkId);
        }
        else {
            Model* m = applied->GetModel();
            CCL_LOG_INFO(LogCategory::Core,
                "[Dungeon][DIAG] POST-APPLY: entity=%u  storedHandle.index=%u valid=%d "
                "isRuntime=%d ownsHandle=%d  GetModel=%p  (registered was index=%u)",
                firstChunkId, applied->modelHandle.index, applied->modelHandle.IsValid(),
                applied->isRuntime, applied->ownsHandle, (void*)m, firstHandle.index);
            if (!applied->modelHandle.IsValid()) {
                CCL_LOG_ERROR(LogCategory::Core,
                    "[Dungeon][DIAG] >>> handle LOST during PendingOps apply. "
                    "The component was COPIED (not moved) and the copy path cleared it, "
                    "OR a temporary's destructor unloaded the instance.");
            }
        }
    }
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

void SceneInitializer::SpawnLightStartSet(CCL::ECS::Core::World &world, ID3D11Device *device)
{
    // ライティングセットアップ
    Prefab::SpawnPrefab(world, "Assets/Prefabs/Light/LightingSetup.json");
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
    EntityFactory::SpawnPrefab(world, "Assets/Prefabs/AnimationModel.json");
   
}

void SceneInitializer::SpawnShaderTest(CCL::ECS::Core::World& world, ID3D11Device* device)
{
    EntityFactory::SpawnPrefab(world, "Assets/Prefabs/Camera/FreeCamera.json");
    EntityFactory::SpawnPrefab(world, "Assets/Prefabs/Sponza.json");
    EntityFactory::SpawnPrefab(world, "Assets/Prefabs/PBRModel.json");
}


#include "Game/Logics/Character/Player/PlayerComponent.h"
#include "Game/Logics/Character/Enemy/EnemyComponent.h"


void SceneInitializer::SpawnEnemySwarmTest(CCL::ECS::Core::World& world, ID3D11Device* device, int count)
{
    // 1. 床の生成 (物理判定付きのステージ)
    EntityFactory::SpawnPrefab(world, "Assets/Prefabs/World/JoltExampleStage.json");

    auto camera = EntityFactory::SpawnPrefab(world, "Assets/Prefabs/Camera/FollowCamera.json");
    auto player = EntityFactory::SpawnPrefab(world, "Assets/Prefabs/Test/PlayerTest.json");
    
    // player.ID() を渡してメソッドチェーンでPatchする
    camera.Patch<CameraBodyFollow>([pid = player.ID()](auto& body) { body.target = pid; })
        .Patch<CameraAimLookAt>([pid = player.ID()](auto& aim) { aim.target = pid; });




    // 4. エネミーのアーキタイプ (★物理判定 JoltRigidbody を追加)
    const auto enemyArch = CCL::ECS::ArchetypeHelper::Generate<
        TransformComponent,
        PrimitiveComponent,
        EnemyComponent,
        JoltRigidbodyComponent,
        JoltSphereColliderComponent
    >();

    std::mt19937 gen(42);
    // プレイヤーの周囲 10m 〜 60m の範囲にドーナツ状に配置する
    std::uniform_real_distribution<float> distAngle(0.0f, DirectX::XM_2PI);
    std::uniform_real_distribution<float> distRadius(10.0f, 60.0f);

    for (int i = 0; i < count; ++i) {
        auto enemy = EntityFactory::SpawnNamed(world, "PhysicsEnemy", enemyArch);

        float angle = distAngle(gen);
        float radius = distRadius(gen);
        float posX = std::cos(angle) * radius;
        float posZ = std::sin(angle) * radius;
        float posY = 5.0f + (i % 10); // 重なりを避けるため少しずつ高さを変えて降らせる

        enemy.Set(TransformComponent{ .position = {posX, posY, posZ}, .rotation = {0.0f, 0.0f, 0.0f, 1.0f}, .scale = {1.0f, 1.0f, 1.0f} })
            .Set(EnemyComponent{})
            .Set(PrimitiveComponent{ .type = PrimitiveType::Sphere, .radius = 1.0f, .color = {1.0f, 0.1f, 0.1f, 1.0f} })
            .Set(JoltSphereColliderComponent{ .radius = 1.0f })
            // 動的剛体として設定（転がりながら向かってくる）
            .Set(JoltRigidbodyComponent{
                .motionType = JPH::EMotionType::Dynamic,
                .objectLayer = PhysicsLayers::MOVING,
                .restitution = 0.1f, // あまり跳ねない
                .friction = 0.2f     // 摩擦
                });
    }
}

// ============================================================================
// 指定した数のモデルを並べるテスト環境
// ============================================================================
void SceneInitializer::SpawnModelGridTest(
    CCL::ECS::Core::World& world, ID3D11Device* device, int count)
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


#include "Game/Logics/Test/LissajousOrbitComponent.h"
#include "Game/Logics/Test/TornadoVortexComponent.h"
#include "Game/Logics/Test/VectorFlowComponent.h"
// ※PrimitiveComponent などのインクルードは適宜行ってください

void SceneInitializer::SpawnLissajousTest(CCL::ECS::Core::World& world, ID3D11Device* device, int count) {
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

void SceneInitializer::SpawnTornadoTest(CCL::ECS::Core::World& world, ID3D11Device* device, int count) {
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

void SceneInitializer::SpawnVectorFlowTest(CCL::ECS::Core::World& world, ID3D11Device* device, int count) {
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