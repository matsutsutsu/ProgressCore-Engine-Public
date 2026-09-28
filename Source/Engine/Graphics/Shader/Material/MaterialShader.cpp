#include "MaterialShader.h"
#include "Engine/Graphics/RHI/GpuResourceUtils.h"
#include "Engine/Graphics/Resource/ModelResource.h"
#include "Engine/Platform/Core/Logger.h"
#include "Engine/Graphics/Shader/ShaderResources.h"

MaterialShader::MaterialShader(ID3D11Device      *device,
                               const std::string &vsCsoPath, const std::wstring &vsHlslPath,
                               const std::string &psCsoPath, const std::wstring &psHlslPath,
                               const PipelineStateDesc *psoDesc)
    : m_vsHlslPath(vsHlslPath), m_psHlslPath(psHlslPath)
{
    if (psoDesc)
    {
        m_pso    = PipelineState(*psoDesc);
        m_hasPso = true;
    }

    GpuResourceUtils::LoadVertexShader(device,
                                       vsCsoPath.c_str(),
                                       ModelResource::InputElementDescs.data(),
                                       (UINT)ModelResource::InputElementDescs.size(),
                                       inputLayout.GetAddressOf(),
                                       vertexShader.GetAddressOf());

    GpuResourceUtils::LoadPixelShader(device, psCsoPath.c_str(), pixelShader.GetAddressOf());

    // 定数バッファはピクセルシェーダーのリフレクションから作る
    InitializeReflection(device, psCsoPath.c_str());

    m_vsLastWriteTime = GetLastWriteTime(m_vsHlslPath);
    m_psLastWriteTime = GetLastWriteTime(m_psHlslPath);
}

void MaterialShader::Begin(ID3D11DeviceContext *dc,
                           const RenderServices & /*s*/,
                           const RenderFrameContext & /*ctx*/)
{
    ApplyShaderPipeline(dc, vertexShader.Get(), pixelShader.Get(), inputLayout.Get());
}

void MaterialShader::Update(ID3D11DeviceContext *dc,
                            const Model::Mesh   &mesh,
                            const MaterialData  *overrideMaterial)
{
    // mesh.material は書き換えない（書くと、上書きをやめた後のフレームにも残る）
    const MaterialData *baseMat = (mesh.material && mesh.material->data) ? mesh.material->data.get() : nullptr;
    const MaterialData *mat     = overrideMaterial ? overrideMaterial : baseMat;

    const MaterialData *fallback = (overrideMaterial && baseMat != overrideMaterial) ? baseMat : nullptr;

    if (mat) UpdateMaterialProperties(dc, *mat, fallback);
}

void MaterialShader::End(ID3D11DeviceContext *dc, const RenderServices & /*s*/)
{
    UnbindResources(dc);
}

void MaterialShader::CheckAndReload(ID3D11Device *device)
{
#if defined(_DEBUG) || defined(ENABLE_EDITOR) // 製品ビルドには HLSL を同梱しないので行わない
    auto currentVsTime = GetLastWriteTime(m_vsHlslPath);

    if (currentVsTime > m_vsLastWriteTime)
    {
        m_vsLastWriteTime = currentVsTime;
        Microsoft::WRL::ComPtr<ID3D11VertexShader> newVertexShader;
        Microsoft::WRL::ComPtr<ID3D11InputLayout>  newInputLayout;

        // コンパイルに失敗したら前のシェーダーのまま
        if (GpuResourceUtils::CompileVertexShader(device, m_vsHlslPath, "main", "vs_5_0",
                                                  newVertexShader.GetAddressOf(),
                                                  newInputLayout.GetAddressOf(),
                                                  ModelResource::InputElementDescs.data(),
                                                  static_cast<UINT>(ModelResource::InputElementDescs.size())))
        {
            vertexShader = newVertexShader;
            inputLayout  = newInputLayout;
            CCL_LOG_SUCCESS(LogCategory::Core, "Hot Reloaded: %ls", m_vsHlslPath.c_str());
        }
    }

    auto currentPsTime = GetLastWriteTime(m_psHlslPath);

    if (currentPsTime > m_psLastWriteTime)
    {
        m_psLastWriteTime = currentPsTime;
        Microsoft::WRL::ComPtr<ID3D11PixelShader> newPixelShader;
        Microsoft::WRL::ComPtr<ID3DBlob>          newBlob;

        // コンパイルに失敗したら前のシェーダーのまま
        if (GpuResourceUtils::CompilePixelShader(device, m_psHlslPath, "main", "ps_5_0",
                                                 newPixelShader.GetAddressOf(), newBlob.GetAddressOf()))
        {
            pixelShader = newPixelShader;

            // 定数バッファの形が変わっている場合があるので、リフレクションから作り直す
            ResetReflection();
            m_reflection.LoadFromBytecode(newBlob->GetBufferPointer(), newBlob->GetBufferSize());

            const auto &cbs = m_reflection.GetConstantBuffers();
            for (const auto &cbInfo : cbs)
            {
                if (cbInfo.bindPoint < SLOT_CB_SCENE)
                {
                    m_constantBuffers[cbInfo.name].Create(device, cbInfo);
                }
            }
            CCL_LOG_SUCCESS(LogCategory::Core, "Hot Reloaded: %ls", m_psHlslPath.c_str());
        }
    }
#endif
}