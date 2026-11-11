#include "ErrorShader.h"
#include "Engine/Graphics/RHI/GpuResourceUtils.h"
#include "Engine/Graphics/Resource/ModelResource.h"

ErrorShader::ErrorShader(ID3D11Device *device)
{
    // モデルの共通の入力レイアウトで読むので、どのマテリアルのメッシュの代わりにも描ける
    GpuResourceUtils::LoadVertexShader(
        device,
        "Assets/Shader/ErrorVS.cso",
        ModelResource::InputElementDescs.data(),
        static_cast<UINT>(ModelResource::InputElementDescs.size()),
        m_inputLayout.GetAddressOf(),
        m_vs.GetAddressOf());

    GpuResourceUtils::LoadPixelShader(device, "Assets/Shader/ErrorPS.cso", m_ps.GetAddressOf());

    // 定数バッファもテクスチャもないが、End で基底の UnbindResources を使うので読んでおく
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