//============================================================================
//  DungeonMeshBatcher.cpp
//============================================================================
#include "DungeonMeshBatcher.h"

#include <DirectXMath.h>

using namespace DirectX;

namespace RuneForge::Dungeon
{
    //------------------------------------------------------------------------
    // 1モデルの全メッシュ頂点を worldX/Y/Z へ平行移動して追記
    //------------------------------------------------------------------------
    void DungeonMeshBatcher::AppendModelAt(
        BatchResult& out,
        const std::shared_ptr<Model>& model,
        float worldX, float worldY, float worldZ,
        uint32_t& indexOffset)
    {
        if (!model) return;

        // 描画と同じく ModelResource 側のメッシュ（CreateFromData も Load もここに入る）
        const ModelResource* resource = model->GetResource();
        if (!resource) return;

        const XMMATRIX tileTransform = XMMatrixTranslation(worldX, worldY, worldZ);

        for (const auto& mesh : resource->GetMeshes())
        {
            // --- 頂点コピー＋座標変換 ---
            for (const auto& v : mesh.vertices)
            {
                ModelResource::Vertex nv = v;

                XMVECTOR pos = XMLoadFloat3(&v.position);
                pos = XMVector3TransformCoord(pos, tileTransform);
                XMStoreFloat3(&nv.position, pos);

                // 平行移動のみなので法線・接線は不変だが、回転対応の余地を残して変換しておく
                XMVECTOR nrm = XMLoadFloat3(&v.normal);
                nrm = XMVector3TransformNormal(nrm, tileTransform);
                XMStoreFloat3(&nv.normal, nrm);

                out.vertices.push_back(nv);
            }

            // --- インデックスコピー（オフセット加算）---
            for (auto idx : mesh.indices)
                out.indices.push_back(idx + indexOffset);

            indexOffset += static_cast<uint32_t>(mesh.vertices.size());
        }
    }

    //------------------------------------------------------------------------
    // チャンク結合本体：床セル＋壁セルを1メッシュへ
    //------------------------------------------------------------------------
    DungeonMeshBatcher::BatchResult DungeonMeshBatcher::BuildChunk(
        const DungeonGenerateResult& result,
        int startI, int startJ, int sizeI, int sizeJ,
        const std::shared_ptr<Model>& floorModel,
        const std::shared_ptr<Model>& wallModel)
    {
        BatchResult out;
        uint32_t indexOffset = 0;

        const float cs = result.cellSize;

        // セル中心のワールド座標（worldX = originX + i*cellSize の規約に +0.5 セル）
        auto centerX = [&](int i) { return result.originX + (i + 0.5f) * cs; };
        auto centerZ = [&](int j) { return result.originZ + (j + 0.5f) * cs; };

        const int i1 = (std::min)(startI + sizeI, result.width);
        const int j1 = (std::min)(startJ + sizeJ, result.depth);

        for (int j = startJ; j < j1; ++j)
        {
            for (int i = startI; i < i1; ++i)
            {
                if (i < 0 || j < 0) continue;

                const bool solid = result.solid[i][j];
                const std::shared_ptr<Model>& model = solid ? wallModel : floorModel;
                if (!model) continue;

                AppendModelAt(out, model, centerX(i), 0.0f, centerZ(j), indexOffset);
            }
        }

        return out;
    }

} // namespace RuneForge::Dungeon