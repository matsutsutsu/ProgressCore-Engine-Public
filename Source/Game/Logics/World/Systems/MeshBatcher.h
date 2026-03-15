#pragma once
#include <DirectXMath.h>
#include <memory>
#include <unordered_map>
#include <vector>

// 必要なヘッダーをインクルード
#include "Game/Logics/World/StageMap.h"
#include "Engine/Graphics/Resource/Model.h"

#include "Engine/Graphics/Resource/ModelResource.h"

// ===================================================================================
// ファイル名: MeshBatcher.h
// クラス名: MeshBatcher (Static Helper)
//
// 【役割・責任】
//   描画負荷（ドローコール）を減らすための「スタティックバッチング」機能を提供します。
//
// 【なぜ必要なのか？】
//   100x100マスのダンジョンを普通に描画すると、壁と床で「20,000回」の描画命令が必要になります。
//   これではPCでも処理落ちします。
//   このクラスは、10x10マスごとの壁を「1つの巨大なモデル」に結合し、
//   描画回数を劇的に（1/100以下に）減らします。
// ===================================================================================



// 頂点フォーマットの定義元（環境に合わせて調整してください）
// GraphicsTypes.h などに Vertex が定義されている場合はそれをインクルード
// ここでは ModelResource 内で使われている頂点型を推論して使います

class MeshBatcher {
  public:
    // 結合結果データ
    struct BatchResult {
        // ModelResource::Vertex が外部からアクセス可能である前提
        // もしアクセスできない場合は、プロジェクト内の共通Vertex構造体を使ってください
        std::vector<ModelResource::Vertex> vertices;
        std::vector<uint32_t>              indices;
    };

    /**
     * @brief 指定された範囲（チャンク）のマップチップを1つのメッシュに結合します
     * @param map           マップデータ
     * @param startX        チャンクの開始X座標 (Grid)
     * @param startZ        チャンクの開始Z座標 (Grid)
     * @param sizeX         チャンクの幅
     * @param sizeZ         チャンクの奥行き
     * @param modelPalette  TileTypeとモデルの対応リスト
     * @return BatchResult  結合された頂点・インデックスデータ
     */
    static BatchResult BuildChunk(const StageMap                   &map,
        int                                                         startX,
        int                                                         startZ,
        int                                                         sizeX,
        int                                                         sizeZ,
        const std::unordered_map<TileType, std::shared_ptr<Model>> &modelPalette);
};