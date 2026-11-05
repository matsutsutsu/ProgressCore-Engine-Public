#include "Engine/Core/Math/Misc.h"
#include "Engine/Graphics/RHI/GpuResourceUtils.h"
#include "Engine/Graphics/Resource/ModelResource.h"
#include "NormalPrepassShader.h"

NormalPrepassShader::NormalPrepassShader(ID3D11Device *device)
{
    GpuResourceUtils::LoadVertexShader(
        device,
        "Assets/Shader/NormalPrepassVS.cso",
        ModelResource::InputElementDescs.data(),
        static_cast<UINT>(ModelResource::InputElementDescs.size()),
        inputLayout.GetAddressOf(),
        vertexShader.GetAddressOf());

    GpuResourceUtils::LoadPixelShader(
        device,
        "Assets/Shader/NormalPrepassPS.cso",
        pixelShader.GetAddressOf());

    InitializeReflection(device, "Assets/Shader/NormalPrepassVS.cso");
}



void NormalPrepassShader::Begin(ID3D11DeviceContext *dc,
                                const RenderServices & /*s*/,
                                const RenderFrameContext & /*ctx*/)
{
    dc->IASetInputLayout(inputLayout.Get());
    dc->VSSetShader(vertexShader.Get(), nullptr, 0);
    dc->PSSetShader(pixelShader.Get(), nullptr, 0);

    for (auto &pair : m_constantBuffers)
    {
        pair.second.BindVS(dc);
    }
}



void NormalPrepassShader::End(ID3D11DeviceContext *dc, const RenderServices & /*s*/)
{
    UnbindResources(dc);
}
