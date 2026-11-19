#pragma once
// ===========================================================================
// 概要: ライトの視点から、カスケードシャドウマップ（3枚）に深度を描くパス
//
// 責務:
//   - カメラとライトの向きから、カスケードごとのビュー・プロジェクション行列を求める
//   - 影を落とすメッシュを集めてメッシュ順に並べ、カスケードごとにインスタンシングで描く
//   - LOD のあるメッシュは、一番粗い段で描く
//   - シャドウマップの定数バッファ（行列・分割距離・バイアスなど）を更新する
// ===========================================================================
#include "Engine/Graphics/Pipeline/Core/IRenderPass.h"
#include "Engine/Graphics/RHI/PipelineState.h"
#include "Engine/Graphics/Renderer/Core/InstanceBuffer.h"
#include "Engine/Graphics/Resource/Model.h"
#include <vector>

class ShadowPass : public IRenderPass
{
public:
    void Initialize(const RenderServices &services) override;
    void Execute(const RenderFrameContext &ctx) override;

private:
    // 影を落とすメッシュ1つ分
    struct SortItem
    {
        const Model::Mesh  *mesh;        // 所有しない
        DirectX::XMFLOAT4X4 worldMatrix; // ノードの行列まで掛けたワールド行列
        uint8_t             cascadeMask; // 描くカスケードのビット（bit i = カスケード i）
    };

    // 同じメッシュデータ（頂点・インデックスバッファ）のインスタンスをまとめたもの
    struct Batch
    {
        const ModelResource::Mesh *meshData = nullptr; // 所有しない
        std::vector<InstanceData>  data;               // インスタンスごとのワールド行列
    };

    PipelineState  m_psoShadow;      // 深度バイアス付きのラスタライザ
    InstanceBuffer m_instanceBuffer; // バッチの行列を頂点シェーダーへ渡す

    // 毎フレーム作り直す作業用の配列
    // clear() は容量を残すので、数フレームで落ち着いた後はメモリを確保しない
    std::vector<SortItem> m_itemsScratch;
    std::vector<Batch>    m_batchesScratch;
    size_t                m_batchCount = 0; // m_batchesScratch のうち、このカスケードで使っている数
};
