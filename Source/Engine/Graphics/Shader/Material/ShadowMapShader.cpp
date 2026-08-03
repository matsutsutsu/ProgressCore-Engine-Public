#include "Engine/Core/Math/Misc.h"
#include "Engine/Graphics/RHI/GpuResourceUtils.h"
#include "ShadowMapShader.h"
#include "Engine/Graphics/Shader/Pass/ShadowMap.h"

ShadowMapShader::ShadowMapShader(ID3D11Device* device)
{
    GpuResourceUtils::LoadVertexShader(
        device,
        "Assets/Shader/ShadowMapVS.cso",
        ModelResource::InputElementDescs.data(),
        static_cast<UINT>(ModelResource::InputElementDescs.size()),
        inputLayout.GetAddressOf(),
        vertexShader.GetAddressOf());

    InitializeReflection(device, "Assets/Shader/ShadowMapVS.cso");
}

void ShadowMapShader::Begin(ID3D11DeviceContext* dc,
    const RenderServices& /*s*/,
    const RenderFrameContext& /*ctx*/)
{
    dc->IASetInputLayout(inputLayout.Get());
    dc->VSSetShader(vertexShader.Get(), nullptr, 0);
    dc->PSSetShader(nullptr, nullptr, 0); // 影書き込みにピクセルシェーダー不要

    for (auto& pair : m_constantBuffers) {
        pair.second.BindVS(dc);
    }
}


void ShadowMapShader::End(ID3D11DeviceContext* dc, const RenderServices& /*s*/)
{
    UnbindResources(dc);
}