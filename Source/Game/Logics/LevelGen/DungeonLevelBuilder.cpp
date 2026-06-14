//============================================================================
//  DungeonLevelBuilder.cpp
//============================================================================
#include "DungeonLevelBuilder.h"
#include "DungeonChunkTag.h"

#include "Engine/ECS/Common/CCL_Common.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/Graphics/Components/Core/ModelComponent.h"
#include "Engine/Graphics/Components/Core/MaterialComponent.h"
#include "Engine/Serialization/Factory/EntityFactory.h"
#include "Engine/Platform/Core/Logger.h"

#include "Engine/Graphics/Resource/Model.h"
#include "Engine/Graphics/Resource/ResourceManager.h"

#include "Game/Logics/LevelGen/DungeonGenerator.h"
#include "Game/Logics/LevelGen/DungeonMeshBatcher.h"
#include "Game/Logics/LevelGen/DungeonGrid.h"
#include "Engine/Serialization/Factory/Prefab.h"

#include <memory>
#include <vector>

using namespace CCL::ECS;
using namespace RuneForge::Dungeon;

bool DungeonLevelBuilder::s_diagnosticLogging = false;

// 診断ログ用マクロ（フラグが立っているときだけ出力）
#define DUNGEON_DIAG(...)                                            \
    do {                                                            \
        if (DungeonLevelBuilder::IsDiagnosticLogging())             \
            CCL_LOG_INFO(LogCategory::Core, __VA_ARGS__);           \
    } while (0)

//----------------------------------------------------------------------------
void DungeonLevelBuilder::Build(CCL::ECS::Core::World& world, ID3D11Device* device,
    uint32_t seed, int floorIndex)
{
    BuildParams params;
    params.seed = seed;
    params.floorIndex = floorIndex;
    Build(world, device, params);
}

//----------------------------------------------------------------------------
void DungeonLevelBuilder::Clear(CCL::ECS::Core::World& world)
{
    // DungeonChunkTag を持つエンティティを集めて破棄予約（ループ中の即時削除を避ける）
    std::vector<EntityID> targets;
    auto view = world.View<DungeonChunkTag>();
    for (auto id : view) targets.push_back(id);

    for (auto id : targets) world.RequestDestroyEntity(id);

    DUNGEON_DIAG("[Dungeon] Clear: requested destroy of %zu chunks", targets.size());
}

//----------------------------------------------------------------------------
void DungeonLevelBuilder::Build(CCL::ECS::Core::World& world, ID3D11Device* device,
    const BuildParams& params)
{
    if (!device)
    {
        CCL_LOG_WARN(LogCategory::Core, "[Dungeon] device is null; abort build.");
        return;
    }

    // ------------------------------------------------------------------
    // 1. 生成（決定的）
    // ------------------------------------------------------------------
    DungeonGenerateResult result =
        DungeonGenerator::Generate(params.seed, params.floorIndex);

    if (DungeonLevelBuilder::IsDiagnosticLogging())
        DungeonGenerator::DebugPrintGrid(result);

    CCL_LOG_INFO(LogCategory::Core,
        "[Dungeon] Building floor %d seed=%u grid=%dx%d (chunk=%d)",
        params.floorIndex, result.usedSeed, result.width, result.depth, params.chunkSize);

    // ------------------------------------------------------------------
    // 2. マップチップモデルをロード（チャンク間で共有）
    // ------------------------------------------------------------------
    auto floorModel = std::make_shared<Model>(device, params.floorModelPath.c_str());
    auto wallModel = std::make_shared<Model>(device, params.wallModelPath.c_str());

    auto countVerts = [](const std::shared_ptr<Model>& m) -> size_t {
        if (!m) return 0;
        const ModelResource* res = m->GetResource();
        if (!res) return 0;
        size_t v = 0;
        for (const auto& mesh : res->GetMeshes()) v += mesh.vertices.size();
        return v;
        };
    const size_t floorVerts = countVerts(floorModel);
    const size_t wallVerts = countVerts(wallModel);

    DUNGEON_DIAG("[Dungeon] chip models: floor='%s' verts=%zu | wall='%s' verts=%zu",
        params.floorModelPath.c_str(), floorVerts,
        params.wallModelPath.c_str(), wallVerts);

    if (floorVerts == 0 && wallVerts == 0)
    {
        CCL_LOG_ERROR(LogCategory::Core,
            "[Dungeon] ABORT: both chip models have 0 vertices. Check .glb paths.");
        return;
    }

    // 代表マテリアル（壁優先、無ければ床）
    std::shared_ptr<MaterialData> floorMat, wallMat;
    if (!floorModel->GetMaterials().empty()) floorMat = floorModel->GetMaterials()[0].data;
    if (!wallModel->GetMaterials().empty())  wallMat = wallModel->GetMaterials()[0].data;
    std::shared_ptr<MaterialData> chunkMat = wallMat ? wallMat : floorMat;

    // ------------------------------------------------------------------
    // 3. チャンクループ：床＋壁を1バッチ→1Model→1エンティティ
    // ------------------------------------------------------------------
    const auto chunkArch = ArchetypeHelper::Generate<
        TransformComponent,
        ModelComponent,
        MaterialComponent,
        DungeonChunkTag        // ★ Clear() で識別するためのタグ
    >();

    int  chunkCount = 0;
    long totalVerts = 0, totalTris = 0;

    for (int cj = 0; cj < result.depth; cj += params.chunkSize)
    {
        for (int ci = 0; ci < result.width; ci += params.chunkSize)
        {
            auto batch = DungeonMeshBatcher::BuildChunk(
                result, ci, cj, params.chunkSize, params.chunkSize, floorModel, wallModel);

            if (batch.vertices.empty()) continue;

            auto chunkModel = Model::CreateFromData(
                device, batch.vertices, batch.indices, chunkMat);
            if (!chunkModel) continue;

            ModelInstanceHandle handle =
                ResourceManager::Instance().RegisterModelInstance(chunkModel);
            if (!handle.IsValid()) continue;

            DUNGEON_DIAG("[Dungeon][DIAG] chunk(%d,%d): handle.index=%u origin=(%.1f,%.1f) verts=%zu",
                ci, cj, handle.index, batch.originX, batch.originZ, batch.vertices.size());

            auto e = EntityFactory::SpawnNamed(world, "DungeonChunk", chunkArch);

            TransformComponent trans{};
            trans.position = { batch.originX, 0.0f, batch.originZ };
            trans.rotation = { 0.0f, 0.0f, 0.0f, 1.0f };
            trans.scale = { 1.0f, 1.0f, 1.0f };
            trans.isStatic = true;
            trans.isDirty = true; // 生成直後に1回 worldMatrix を計算させる

            ModelComponent mc{};
            mc.SetRuntimeModel(handle);

            e.Set(std::move(trans))
                .Set(std::move(mc));

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

    // ------------------------------------------------------------------
    // 4. 衝突・問い合わせ用グリッドを World リソースとして登録
    //    弾・敵・AI が world.GetResource<DungeonGrid>() で参照する。
    // ------------------------------------------------------------------
    if (params.registerGridResource)
    {
        if (world.HasResource<DungeonGrid>())
            world.GetResource<DungeonGrid>() = DungeonGrid::FromResult(result);
        else
            world.AddResource<DungeonGrid>(DungeonGrid::FromResult(result));

        DUNGEON_DIAG("[Dungeon] DungeonGrid resource registered (%dx%d).",
            result.width, result.depth);
    }

    // ------------------------------------------------------------------
    // 5. スポーンマーカーから Prefab を配置
    // ------------------------------------------------------------------
    if (params.spawnPrefabs)
    {
        SpawnMarkers(world, result, params);
    }

    if (params.applyImmediately)
        world.ScrutinyAndApply();
}

//----------------------------------------------------------------------------
// スポーンマーカー → Prefab 配置
//----------------------------------------------------------------------------
void DungeonLevelBuilder::SpawnMarkers(CCL::ECS::Core::World& world,
    const DungeonGenerateResult& result,
    const BuildParams& params)
{
    // マーカー種 → Prefab パスを引く
    auto pathFor = [&](SpawnMarkerType t) -> const std::string& {
        static const std::string empty;
        switch (t)
        {
        case SpawnMarkerType::Enemy:      return params.enemyPrefab;
        case SpawnMarkerType::Chest:      return params.chestPrefab;
        case SpawnMarkerType::Shop:       return params.shopPrefab;
        case SpawnMarkerType::Forge:      return params.forgePrefab;
        case SpawnMarkerType::Portal:     return params.portalPrefab;
        case SpawnMarkerType::Boss:       return params.bossPrefab;
        case SpawnMarkerType::StartPoint: return params.playerPrefab;
        default:                          return empty;
        }
        };

    const float cs = result.cellSize;
    auto worldX = [&](int i) { return result.originX + (i + 0.5f) * cs; };
    auto worldZ = [&](int j) { return result.originZ + (j + 0.5f) * cs; };

    int spawned = 0, skipped = 0;
    for (const RoomMeta& room : result.rooms)
    {
        for (const SpawnMarker& m : room.spawnMarkers)
        {
            const std::string& path = pathFor(m.type);
            if (path.empty()) { ++skipped; continue; } // パス未設定はスキップ

            DirectX::XMFLOAT3 pos{ worldX(m.cell.i), 0.0f, worldZ(m.cell.j) };
            Prefab::SpawnPrefab(world, path, pos);
            ++spawned;
        }
    }

    CCL_LOG_INFO(LogCategory::Core,
        "[Dungeon] Spawned %d prefabs from markers (%d skipped: no path).",
        spawned, skipped);
}