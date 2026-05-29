#include "Engine/Core/Math/Misc.h"
#include "Engine/Graphics/RHI/GpuResourceUtils.h"
#include "OutlineShader.h"
#include "Engine/Graphics/RHI/RenderState.h"
// Graphics.h インクルード不要（Graphics::Instance() を完全除去）

OutlineShader::OutlineShader(ID3D11Device* device)
{
    GpuResourceUtils::LoadVertexShader(device, "Assets/Shader/OutlineVS.cso",
        ModelResource::InputElementDescs.data(),
        static_cast<UINT>(ModelResource::InputElementDescs.size()),
        inputLayout.GetAddressOf(), vertexShader.GetAddressOf());

    GpuResourceUtils::LoadPixelShader(device, "Assets/Shader/OutlinePS.cso",
        pixelShader.GetAddressOf());

    InitializeReflection(device, "Assets/Shader/OutlineVS.cso");

    D3D11_RASTERIZER_DESC rs{};
    rs.FillMode = D3D11_FILL_SOLID;
    rs.CullMode = D3D11_CULL_FRONT;
    rs.DepthClipEnable = TRUE;
    device->CreateRasterizerState(&rs, m_rasterizerState.GetAddressOf());
}

void OutlineShader::Begin(ID3D11DeviceContext* dc,
    const RenderServices& /*s*/,
    const RenderFrameContext& /*ctx*/)
{
    ApplyShaderPipeline(dc, vertexShader.Get(), pixelShader.Get(), inputLayout.Get());
    dc->RSSetState(m_rasterizerState.Get());
}

void OutlineShader::Update(ID3D11DeviceContext* dc, const Model::Mesh& mesh, const MaterialData* overrideMaterial)
{
    const MaterialData* mat = overrideMaterial
        ? overrideMaterial
        : (mesh.material && mesh.material->data ? mesh.material->data.get() : nullptr);
    if (mat) UpdateMaterialProperties(dc, *mat);
}

void OutlineShader::End(ID3D11DeviceContext* dc, const RenderServices& s)
{
    // Graphics::Instance() を廃止し、s.renderState 経由でラスタライザを復元する。
    if (s.renderState) {
        dc->RSSetState(s.renderState->GetRasterizerState(RasterizerState::SolidCullBack));
    }
    UnbindResources(dc);
}