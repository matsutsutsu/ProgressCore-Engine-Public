#include "MaterialShader.h"
#include "Engine/Graphics/RHI/GpuResourceUtils.h"
#include "Engine/Graphics/Resource/ModelResource.h"

MaterialShader::MaterialShader(ID3D11Device* device, const std::string& vsPath, const std::string& psPath)
{
    // 汎用頂点レイアウトを使用して頂点シェーダーをロード
    GpuResourceUtils::LoadVertexShader(device,
        vsPath.c_str(),
        ModelResource::InputElementDescs.data(),
        (UINT)ModelResource::InputElementDescs.size(),
        inputLayout.GetAddressOf(),
        vertexShader.GetAddressOf());

    // ピクセルシェーダーのロード
    GpuResourceUtils::LoadPixelShader(device, psPath.c_str(), pixelShader.GetAddressOf());

    // リフレクションの初期化（これを呼ぶことで定数バッファの変数がすべて自動解析される）
    InitializeReflection(device, psPath.c_str());
}

void MaterialShader::Begin(const RenderContext& rc)
{
    ApplyShaderPipeline(rc.deviceContext, vertexShader.Get(), pixelShader.Get(), inputLayout.Get());
}

void MaterialShader::Update(const RenderContext& rc, const Model::Mesh& mesh)
{
    // マテリアルのパラメータをハッシュベースで定数バッファへ超高速転送
    if (mesh.material && mesh.material->data) {
        UpdateMaterialProperties(rc.deviceContext, *mesh.material->data);
    }
}

void MaterialShader::End(const RenderContext& rc)
{
    UnbindResources(rc.deviceContext);
}