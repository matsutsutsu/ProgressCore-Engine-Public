#include "MeshBatcher.h"

// ===================================================================================
// ファイル名: MeshBatcher.cpp
//
// 【役割・責任】
//   モデルの頂点データ（位置、法線、UVなど）をメモリ上で結合して、
//   新しい巨大なメッシュデータを生成する実装です。
//
// 【処理内容】
//   BuildChunk関数:
//     指定された範囲（例:座標0,0から10,10まで）にある壁や床を探し、
//     それぞれのワールド座標に合わせて頂点を移動させてから、1つのリストにまとめます。
// ===================================================================================

using namespace DirectX;

MeshBatcher::BatchResult MeshBatcher::BuildChunk(const StageMap &map,
    int                                                          startX,
    int                                                          startZ,
    int                                                          sizeX,
    int                                                          sizeZ,
    const std::unordered_map<TileType, std::shared_ptr<Model>>  &modelPalette)
{
    MeshBatcher::BatchResult result;
    uint32_t                 indexOffset = 0; // インデックス結合用のオフセット

    // 指定されたチャンク範囲をループ
    for (int z = 0; z < sizeZ; ++z) {
        for (int x = 0; x < sizeX; ++x) {

            // マップ全体の絶対座標
            int mapX = startX + x;
            int mapZ = startZ + z;

            // マップ範囲外チェック
            if (mapX < 0 || mapX >= map.width || mapZ < 0 || mapZ >= map.height) continue;

            // タイル情報を取得
            int      tileIdx = map.GetIndex(mapX, mapZ);
            TileType type    = map.tiles[tileIdx];

            // 描画対象外のタイルはスキップ
            if (type == TileType::None || type == TileType::Hole) continue;

            // パレットからモデルを検索
            auto it = modelPalette.find(type);
            if (it == modelPalette.end()) continue;

            std::shared_ptr<Model> srcModel = it->second;
            if (!srcModel) continue;

            // 【重要】ModelResourceを取得して頂点データへアクセス
            const ModelResource *resource = srcModel->GetResource();
            if (!resource) continue;

            // 配置するワールド座標（マスの中心）を取得
            auto worldPos = map.GridToWorld(mapX, mapZ);

            // 平行移動行列を作成
            // (必要であればここで回転行列も掛け合わせることができます)
            XMMATRIX tileTransform = XMMatrixTranslation(worldPos.x, worldPos.y, worldPos.z);

            // リソース内の全メッシュを結合対象にする
            for (const auto &mesh : resource->GetMeshes()) {

                // --- 頂点のコピーと座標変換 ---
                for (const auto &v : mesh.vertices) {
                    // 頂点データをコピー
                    auto newV = v;

                    // 1. 位置座標の変換 (Local -> World)
                    XMVECTOR pos = XMLoadFloat3(&v.position);
                    pos          = XMVector3TransformCoord(pos, tileTransform);
                    XMStoreFloat3(&newV.position, pos);

                    // 2. 法線ベクトルの変換
                    // (回転のみ適用、今回は平行移動だけなので変化なしだが念のため)
                    XMVECTOR norm = XMLoadFloat3(&v.normal);
                    norm          = XMVector3TransformNormal(norm, tileTransform);
                    XMStoreFloat3(&newV.normal, norm);

                    // (必要ならTangentなども同様に変換)

                    result.vertices.push_back(newV);
                }

                // --- インデックスのコピー ---
                for (auto idx : mesh.indices) {
                    // 結合前の頂点数分だけオフセットを足す
                    result.indices.push_back(idx + indexOffset);
                }

                // 次のメッシュのためにオフセットを進める
                indexOffset += static_cast<uint32_t>(mesh.vertices.size());
            }
        }
    }

    return result;
}