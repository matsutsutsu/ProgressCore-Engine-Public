#pragma once
//============================================================================
//  DungeonMeshBatcher.h
//  RuneForge ダンジョン — チャンク・メッシュ結合（前回チーム MeshBatcher の発展版）
//
//  【前回チームとの違い】
//   - 床と壁を別パレット・別バッチにせず、1チャンク内で「床セル＝床モデル、
//     壁セル＝壁モデル」を同一頂点バッファへ溶接して 1 メッシュにまとめる。
//   - 入力は StageMap ではなく DungeonGenerateResult（solid グリッド）。
//
//  生成した頂点・インデックスは Model::CreateFromData に渡してModel化する。
//============================================================================
#include "DungeonTypes.h"

#include "Engine/Graphics/Resource/Model.h"          // ModelResource::Vertex / Model
#include "Engine/Graphics/Resource/ModelResource.h"

#include <memory>
#include <vector>

namespace RuneForge::Dungeon
{
    class DungeonMeshBatcher
    {
    public:
        struct BatchResult
        {
            std::vector<ModelResource::Vertex> vertices;
            std::vector<uint32_t>              indices;
            // このチャンクのワールド原点（頂点はこの原点からのローカル座標）。
            // エンティティの TransformComponent.position にこれを入れる。
            float originX = 0.0f;
            float originZ = 0.0f;
        };

        // 1チャンク分（[startI,startI+sizeI) x [startJ,startJ+sizeJ)）の
        // 床セル＋壁セルを、それぞれのモデルから頂点転写して1メッシュに結合する。
        //
        //   result    : 生成元のダンジョン（solid と cellSize/origin を使用）
        //   floorModel: 床セルに敷くマップチップモデル（例 Yuka.glb）
        //   wallModel : 壁セルに敷くマップチップモデル（例 Wall.glb）
        //
        // モデルのローカル原点は「セル中心」想定。各セルのワールド中心へ平行移動する。
        static BatchResult BuildChunk(
            const DungeonGenerateResult& result,
            int startI, int startJ, int sizeI, int sizeJ,
            const std::shared_ptr<Model>& floorModel,
            const std::shared_ptr<Model>& wallModel);

    private:
        // 1つのモデルの全メッシュ頂点を、指定ワールド座標へ平行移動して result に追記
        static void AppendModelAt(
            BatchResult& out,
            const std::shared_ptr<Model>& model,
            float worldX, float worldY, float worldZ,
            uint32_t& indexOffset);
    };

} // namespace RuneForge::Dungeon