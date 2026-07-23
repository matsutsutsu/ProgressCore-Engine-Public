#pragma once
#include "Engine/Graphics/Pipeline/Core/IRenderPass.h"
#include "Engine/Graphics/RHI/PipelineState.h"
#include "Engine/Graphics/Renderer/InstanceBuffer.h"
#include "Engine/Graphics/Resource/Model.h"
#include <vector>

/**
 * @brief ライト視点でシャドウマップ（カスケード 3 枚）を生成するパス。
 */
class ShadowPass : public IRenderPass
{
public:
    void Initialize(const RenderServices& services) override;
    void Execute(const RenderFrameContext& ctx) override;
    const char* GetName() const override { return "ShadowPass"; }

private:
    // カスケード毎のソート/バッチ処理用の型。
    // メンバのスクラッチ領域(_itemsScratch/_batchesScratch)でしか使わないためここに置く。
    struct SortItem {
        const Model::Mesh* mesh;
        DirectX::XMFLOAT4X4 worldMatrix;
        uint8_t cascadeMask;
    };
    struct Batch {
        const Model::Mesh* mesh = nullptr;
        std::vector<InstanceData> data;
    };

    PipelineState  _psoShadow;
    InstanceBuffer _instanceBuffer;

    // ★毎フレーム(カスケード×3回)の std::vector 新規確保を避けるためのスクラッチ領域。
    //   OpaquePass::_instanceScratch や RenderTargetVisualizer::_pool と同じ「使い回し」方式。
    //   .clear() は capacity を維持するため、数フレームで確保が収束しそれ以降は再確保されない。
    std::vector<SortItem> _itemsScratch;
    std::vector<Batch>    _batchesScratch;
    size_t                _batchCount = 0; // このフレームで実際に使っている _batchesScratch の要素数
};