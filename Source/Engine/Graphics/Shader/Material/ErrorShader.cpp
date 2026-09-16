// ErrorShader の実装。標準モデルレイアウトで頂点を変換し、PSで一律マゼンタを出力する。
#include "ErrorShader.h"
#include "Engine/Graphics/RHI/GpuResourceUtils.h"
#include "Engine/Graphics/Resource/ModelResource.h"

ErrorShader::ErrorShader(ID3D11Device *device)
{
    // 標準モデルレイアウトでロードする理由:
    // Opaque/Transparent が用意する頂点・インスタンスバッファをそのまま流用でき、
    // どのマテリアルシェーダーの代替としても破綻なく描画できるため。
    GpuResourceUtils::LoadVertexShader(
        device,
        "Assets/Shader/ErrorVS.cso",
        ModelResource::InputElementDescs.data(),
        static_cast<UINT>(ModelResource::InputElementDescs.size()),
        m_inputLayout.GetAddressOf(),
        m_vs.GetAddressOf());

    GpuResourceUtils::LoadPixelShader(device, "Assets/Shader/ErrorPS.cso", m_ps.GetAddressOf());

    // CB/テクスチャは持たないが、基底の解放処理(UnbindResources)を共通化するため呼ぶ。
    InitializeReflection(device, "Assets/Shader/ErrorPS.cso");
}

void ErrorShader::Begin(ID3D11DeviceContext *dc, const RenderServices &, const RenderFrameContext &)
{
    ApplyShaderPipeline(dc, m_vs.Get(), m_ps.Get(), m_inputLayout.Get());
}

void ErrorShader::End(ID3D11DeviceContext *dc, const RenderServices &)
{
    UnbindResources(dc);
}