#pragma once
// ===========================================================================
// 概要: 半透明のメッシュを、カメラから遠い順に1つずつ描くパス。
//
// 責務:
//   - OpaquePass が RenderQueue に回した半透明のコマンドを、距離の降順に並べる
//   - 1メッシュずつマテリアルを更新して描く
//
// 備考: 手前のものが奥のものに正しく重なるように、遠い順に描く必要がある。
//       並び順がインスタンスごとに違うので、インスタンシングでまとめて描かない。
// ===========================================================================
#include "Engine/Graphics/Pipeline/Core/IRenderPass.h"
#include "Engine/Graphics/RHI/PipelineState.h"
#include "Engine/Graphics/Renderer/InstanceBuffer.h"

class TransparentPass : public IRenderPass
{
public:
    void Initialize(const RenderServices &services) override;
    void Execute(const RenderFrameContext &ctx) override;

private:
    PipelineState  m_psoTransparent; // シェーダーが PSO を持たないときに使う既定
    InstanceBuffer m_instanceBuffer;
};
