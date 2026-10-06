#pragma once
// ===========================================================================
// 概要: ライトの視点から、カスケードシャドウマップ（3枚）に深度を描くパス。
//
// 責務:
//   - カメラとライトの向きから、カスケードごとのビュー・プロジェクション行列を求める
//   - 影を落とすメッシュを集めてメッシュ順に並べ、カスケードごとにインスタンシングで描く
//   - シャドウマップの定数バッファ（行列・分割距離・バイアスなど）を更新する
//
// 備考: スキンメッシュはボーン行列が1体ごとに違うので、まとめずに1体ずつ描く。
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

    // 同じメッシュのインスタンスをまとめたもの
    struct Batch
    {
        const Model::Mesh        *mesh = nullptr; // 所有しない
        std::vector<InstanceData> data;
    };

    PipelineState  m_psoShadow;
    InstanceBuffer m_instanceBuffer;

    // 毎フレーム作り直す作業用の配列。clear() は確保済みの容量を残すので、
    // 数フレームで大きさが落ち着き、それ以降はメモリを確保しない
    std::vector<SortItem> m_itemsScratch;
    std::vector<Batch>    m_batchesScratch;
    size_t                m_batchCount = 0; // m_batchesScratch のうち、このカスケードで使っている数
};
