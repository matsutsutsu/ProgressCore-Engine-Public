#include "NavMeshBuilder.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/Graphics/Components/Core/ModelComponent.h"
#include "Engine/Graphics/Resource/Model.h"
#include "Engine/Platform/Core/Logger.h"

#include "Engine/AI/Navigation/Core/NavigationData.h"

#include <Recast.h>
#include <DetourNavMesh.h>
#include <DetourNavMeshBuilder.h>

namespace
{
    // Recast の中間データ。どこで失敗しても、確保したものだけを確実に解放するためにまとめて持つ。
    // rcFree* は nullptr を渡しても何もしない。
    struct RecastWork
    {
        rcHeightfield        *solid = nullptr;
        rcCompactHeightfield *chf   = nullptr;
        rcContourSet         *cset  = nullptr;
        rcPolyMesh           *pmesh = nullptr;
        rcPolyMeshDetail     *dmesh = nullptr;

        ~RecastWork()
        {
            rcFreeHeightField(solid);
            rcFreeCompactHeightfield(chf);
            rcFreeContourSet(cset);
            rcFreePolyMesh(pmesh);
            rcFreePolyMeshDetail(dmesh);
        }
    };
} // namespace

NavMeshBuildData NavMeshBuilder::GatherGeometry(CCL::ECS::World &world)
{
    NavMeshBuildData data;
    int              vertexOffset = 0; // 複数のモデル・メッシュを1つの配列へつなぐための頂点オフセット

    std::vector<CCL::ECS::EntityID> entities = world.View<TransformComponent, ModelComponent>();

    for (CCL::ECS::EntityID entity : entities)
    {
        TransformComponent *transform = world.GetComponent<TransformComponent>(entity);
        ModelComponent     *modelComp = world.GetComponent<ModelComponent>(entity);
        if (!transform || !modelComp) continue;

        // 動くもの（キャラクター等）は NavMesh に含めない。isStatic の地形だけを使う
        if (!transform->isStatic)
        {
            continue;
        }

        Model *model = modelComp->GetModel();
        if (!model) continue;

        const ModelResource *resource = model->GetResource();
        if (!resource) continue;

        DirectX::XMMATRIX worldMatrix = DirectX::XMLoadFloat4x4(&transform->worldMatrix);

        for (const auto &mesh : resource->GetMeshes())
        {
            // 頂点をワールド座標へ変換し、Recast が受け取る xyz の並んだ float 配列に詰める
            for (const auto &v : mesh.vertices)
            {
                DirectX::XMVECTOR localPos = DirectX::XMVectorSet(v.position.x, v.position.y, v.position.z, 1.0f);
                DirectX::XMVECTOR worldPos = DirectX::XMVector3TransformCoord(localPos, worldMatrix);

                DirectX::XMFLOAT3 wp;
                DirectX::XMStoreFloat3(&wp, worldPos);

                data.vertices.push_back(wp.x);
                data.vertices.push_back(wp.y);
                data.vertices.push_back(wp.z);
            }

            // インデックスは、それまでに追加した頂点数を足す。
            // 足さないと、別のモデル・メッシュの頂点を指してしまう
            for (UINT idx : mesh.indices)
            {
                data.indices.push_back(static_cast<int>(idx) + vertexOffset);
            }

            vertexOffset += static_cast<int>(mesh.vertices.size());
        }
    }

    return data;
}

bool NavMeshBuilder::BuildNavMesh(CCL::ECS::World &world, const NavMeshBuildSettings &settings)
{
    // 1. ジオメトリの収集
    NavMeshBuildData geom = GatherGeometry(world);
    // Recast の API が int を取るため明示的に縮める。ナビメッシュ用ジオメトリが
    // int を溢れる規模になることは無い。
    const int nverts = static_cast<int>(geom.vertices.size() / 3);
    const int ntris  = static_cast<int>(geom.indices.size() / 3);

    if (nverts == 0 || ntris == 0)
    {
        CCL_LOG_ERROR(LogCategory::Core, "NavMesh Build Failed: No static geometry found.");
        return false;
    }

    rcContext  ctx; // Recast のログ出力・計時の受け口
    RecastWork work;

    // 2. 地形全体のバウンディングボックス
    float bmin[3], bmax[3];
    rcCalcBounds(geom.vertices.data(), nverts, bmin, bmax);

    // 3. rcConfig の設定。メートル単位の設定値を、Recast のセル（ボクセル）数に変換する
    rcConfig cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.cs                     = settings.cellSize;
    cfg.ch                     = settings.cellHeight;
    cfg.walkableSlopeAngle     = settings.agentMaxSlope;
    cfg.walkableHeight         = (int)ceilf(settings.agentHeight / cfg.ch);
    cfg.walkableClimb          = (int)floorf(settings.agentMaxClimb / cfg.ch);
    cfg.walkableRadius         = (int)ceilf(settings.agentRadius / cfg.cs);
    cfg.maxEdgeLen             = (int)(settings.edgeMaxLen / settings.cellSize);
    cfg.maxSimplificationError = settings.edgeMaxError;
    cfg.minRegionArea          = (int)rcSqr(settings.regionMinSize);
    cfg.mergeRegionArea        = (int)rcSqr(settings.regionMergeSize);
    cfg.maxVertsPerPoly        = (int)settings.vertsPerPoly;
    cfg.detailSampleDist       = settings.detailSampleDist < 0.9f ? 0 : settings.cellSize * settings.detailSampleDist;
    cfg.detailSampleMaxError   = settings.cellHeight * settings.detailSampleMaxError;
    rcVcopy(cfg.bmin, bmin);
    rcVcopy(cfg.bmax, bmax);

    // X 軸と Z 軸のセル数
    rcCalcGridSize(cfg.bmin, cfg.bmax, cfg.cs, &cfg.width, &cfg.height);

    CCL_LOG_INFO(LogCategory::Core, "NavMesh Voxel Grid Size: %d x %d", cfg.width, cfg.height);

    // 4. ハイトフィールド（ボクセル空間）の確保
    work.solid = rcAllocHeightfield();
    if (!work.solid || !rcCreateHeightfield(&ctx, *work.solid, cfg.width, cfg.height, cfg.bmin, cfg.bmax, cfg.cs, cfg.ch))
    {
        CCL_LOG_ERROR(LogCategory::Core, "NavMesh Build Failed: Could not create solid heightfield.");
        return false;
    }

    // 5. 歩ける三角形の判定とラスタライズ。
    //    法線の傾きが walkableSlopeAngle を超える三角形は歩けないものとして印を付ける
    std::vector<unsigned char> triAreas(ntris, 0);
    rcMarkWalkableTriangles(&ctx, cfg.walkableSlopeAngle, geom.vertices.data(), nverts, geom.indices.data(), ntris, triAreas.data());

    if (!rcRasterizeTriangles(&ctx, geom.vertices.data(), nverts, geom.indices.data(), triAreas.data(), ntris, *work.solid, cfg.walkableClimb))
    {
        CCL_LOG_ERROR(LogCategory::Core, "NavMesh Build Failed: Could not rasterize triangles.");
        return false;
    }

    // 6. 通れない場所を歩行不可にする（低い障害物の上・崖の縁・天井が低い場所）
    rcFilterLowHangingWalkableObstacles(&ctx, cfg.walkableClimb, *work.solid);
    rcFilterLedgeSpans(&ctx, cfg.walkableHeight, cfg.walkableClimb, *work.solid);
    rcFilterWalkableLowHeightSpans(&ctx, cfg.walkableHeight, *work.solid);

    CCL_LOG_INFO(LogCategory::Core, "NavMesh Rasterization Success!");

    // ============================================================
    // 領域分割と PolyMesh の構築
    // ============================================================

    // 7. コンパクトハイトフィールドへの変換。以降の処理はこちらの方が速い
    work.chf = rcAllocCompactHeightfield();
    if (!work.chf || !rcBuildCompactHeightfield(&ctx, cfg.walkableHeight, cfg.walkableClimb, *work.solid, *work.chf))
    {
        CCL_LOG_ERROR(LogCategory::Core, "NavMesh Build Failed: Could not build compact heightfield.");
        return false;
    }
    // 元のハイトフィールドはもう使わないので、ここで解放してメモリを空ける
    rcFreeHeightField(work.solid);
    work.solid = nullptr;

    // 8. 壁からエージェントの半径（walkableRadius）ぶん、歩ける領域を削る
    if (!rcErodeWalkableArea(&ctx, cfg.walkableRadius, *work.chf))
    {
        CCL_LOG_ERROR(LogCategory::Core, "NavMesh Build Failed: Could not erode walkable area.");
        return false;
    }

    // 9. 領域（Region）の分割（Watershed 法）
    if (!rcBuildDistanceField(&ctx, *work.chf))
    {
        CCL_LOG_ERROR(LogCategory::Core, "NavMesh Build Failed: Could not build distance field.");
        return false;
    }
    if (!rcBuildRegions(&ctx, *work.chf, 0, cfg.minRegionArea, cfg.mergeRegionArea))
    {
        CCL_LOG_ERROR(LogCategory::Core, "NavMesh Build Failed: Could not build regions.");
        return false;
    }

    // 10. 領域の境界をなぞって輪郭線（Contour）を作る
    work.cset = rcAllocContourSet();
    if (!work.cset || !rcBuildContours(&ctx, *work.chf, cfg.maxSimplificationError, cfg.maxEdgeLen, *work.cset))
    {
        CCL_LOG_ERROR(LogCategory::Core, "NavMesh Build Failed: Could not build contours.");
        return false;
    }

    // 11. 輪郭線から、経路探索のノードになる凸ポリゴン（PolyMesh）を作る
    work.pmesh = rcAllocPolyMesh();
    if (!work.pmesh || !rcBuildPolyMesh(&ctx, *work.cset, cfg.maxVertsPerPoly, *work.pmesh))
    {
        CCL_LOG_ERROR(LogCategory::Core, "NavMesh Build Failed: Could not build poly mesh.");
        return false;
    }

    // 歩けるポリゴンにフラグを立てる
    for (int i = 0; i < work.pmesh->npolys; ++i)
    {
        if (work.pmesh->areas[i] == RC_WALKABLE_AREA)
        {
            work.pmesh->areas[i] = 0; // Detour の既定のエリア（0）として扱う
            work.pmesh->flags[i] = 1; // 0 のままだと、探索時のフィルタ（dtQueryFilter の既定値）で除外される
        }
    }

    // 12. 詳細メッシュ（Detail Mesh）。ポリゴンの表面に地形の起伏を持たせ、
    //     経路上の高さ（Y）を地面に合わせられるようにする
    work.dmesh = rcAllocPolyMeshDetail();
    if (!work.dmesh || !rcBuildPolyMeshDetail(&ctx, *work.pmesh, *work.chf, cfg.detailSampleDist, cfg.detailSampleMaxError, *work.dmesh))
    {
        CCL_LOG_ERROR(LogCategory::Core, "NavMesh Build Failed: Could not build detail mesh.");
        return false;
    }

    CCL_LOG_INFO(LogCategory::Core, "NavMesh PolyMesh Generated: %d Polygons", work.pmesh->npolys);

    // ============================================================
    // Detour のナビメッシュデータの作成
    // ============================================================
    // pmesh（ポリゴン）と dmesh（詳細な起伏）をまとめ、Detour が探索に使うバイナリにする

    dtNavMeshCreateParams params;
    memset(&params, 0, sizeof(params));

    params.verts     = work.pmesh->verts;
    params.vertCount = work.pmesh->nverts;
    params.polys     = work.pmesh->polys;
    params.polyAreas = work.pmesh->areas;
    params.polyFlags = work.pmesh->flags;
    params.polyCount = work.pmesh->npolys;
    params.nvp       = work.pmesh->nvp;

    params.detailMeshes     = work.dmesh->meshes;
    params.detailVerts      = work.dmesh->verts;
    params.detailVertsCount = work.dmesh->nverts;
    params.detailTris       = work.dmesh->tris;
    params.detailTriCount   = work.dmesh->ntris;

    // エージェントの大きさ（m）
    params.walkableHeight = settings.agentHeight;
    params.walkableRadius = settings.agentRadius;
    params.walkableClimb  = settings.agentMaxClimb;
    rcVcopy(params.bmin, work.pmesh->bmin);
    rcVcopy(params.bmax, work.pmesh->bmax);
    params.cs          = cfg.cs;
    params.ch          = cfg.ch;
    params.buildBvTree = true; // 最寄りポリゴンの検索を速くする BV ツリーを作る

    unsigned char *navData     = 0;
    int            navDataSize = 0;

    if (!dtCreateNavMeshData(&params, &navData, &navDataSize))
    {
        CCL_LOG_ERROR(LogCategory::Core, "NavMesh Build Failed: Could not build Detour navmesh.");
        return false;
    }

    CCL_LOG_INFO(LogCategory::Core, "Detour NavMesh Binary Created: %d bytes", navDataSize);

    // navData はファイルへ保存していない。Bake のたびに作り直し、そのまま実行時の dtNavMesh にする
    dtNavMesh *runtimeNavMesh = dtAllocNavMesh();
    if (!runtimeNavMesh)
    {
        CCL_LOG_ERROR(LogCategory::Core, "Failed to allocate Detour NavMesh");
        dtFree(navData);
        return false;
    }

    // DT_ALLOC_PERM を指定すると、init が成功した時点で runtimeNavMesh が navData を所有する
    dtStatus status = runtimeNavMesh->init(navData, navDataSize, DT_ALLOC_PERM);

    if (dtStatusFailed(status))
    {
        CCL_LOG_ERROR(LogCategory::Core, "Failed to init Detour NavMesh");
        dtFree(navData);
        dtFreeNavMesh(runtimeNavMesh);
        return false;
    }

    // 生成した NavMesh は World のリソースとして共有する
    if (!world.HasResource<NavigationData>())
    {
        world.AddResource<NavigationData>();
    }
    else
    {
        // 2回目以降の Bake では、前回のデータを解放してから差し替える
        auto &oldData = world.GetResource<NavigationData>();
        if (oldData.navQuery)
        {
            dtFreeNavMeshQuery(oldData.navQuery);
            oldData.navQuery = nullptr;
        }
        if (oldData.navMesh)
        {
            dtFreeNavMesh(oldData.navMesh);
            oldData.navMesh = nullptr;
        }
    }

    world.GetResource<NavigationData>().navMesh = runtimeNavMesh;

    // ============================================================
    // 経路探索（NavMeshQuery）の初期化
    // ============================================================
    dtNavMeshQuery *runtimeNavQuery = dtAllocNavMeshQuery();
    // 2048 は探索時に使うノード数の上限（A* のオープンリスト・クローズドリストの大きさ）
    dtStatus queryStatus = runtimeNavQuery->init(runtimeNavMesh, 2048);

    if (dtStatusFailed(queryStatus))
    {
        CCL_LOG_ERROR(LogCategory::Core, "Failed to init Detour NavMeshQuery");
        dtFreeNavMeshQuery(runtimeNavQuery);
        return false;
    }

    world.GetResource<NavigationData>().navQuery = runtimeNavQuery;

    return true;
}
