#include "MaterialShader.h"
#include "Engine/Graphics/RHI/GpuResourceUtils.h"
#include "Engine/Graphics/Resource/ModelResource.h"
#include "Engine/Platform/Core/Logger.h"

MaterialShader::MaterialShader(ID3D11Device *device,
    const std::string &vsCsoPath, const std::wstring &vsHlslPath,
    const std::string &psCsoPath, const std::wstring &psHlslPath,
    const PipelineStateDesc *psoDesc) 
    : _vsHlslPath(vsHlslPath), _psHlslPath(psHlslPath)
{
    // JSON で PSO 指定があれば固める（無ければパス既定に委ねる）
    if (psoDesc)
    {
        _pso    = PipelineState(*psoDesc);
        _hasPso = true;
    }

    
    // 汎用頂点レイアウトを使用して頂点シェーダーをロード
    GpuResourceUtils::LoadVertexShader(device,
        vsCsoPath.c_str(),
        ModelResource::InputElementDescs.data(),
        (UINT)ModelResource::InputElementDescs.size(),
        inputLayout.GetAddressOf(),
        vertexShader.GetAddressOf());

    // ピクセルシェーダーのロード
    GpuResourceUtils::LoadPixelShader(device, psCsoPath.c_str(), pixelShader.GetAddressOf());

    // リフレクションの初期化（これを呼ぶことで定数バッファの変数がすべて自動解析される）
    InitializeReflection(device, psCsoPath.c_str());

    // 初期化時のタイムスタンプを記録
    _vsLastWriteTime = GetLastWriteTime(_vsHlslPath);
    _psLastWriteTime = GetLastWriteTime(_psHlslPath);
}


void MaterialShader::Begin(ID3D11DeviceContext* dc,
    const RenderServices& /*s*/,
    const RenderFrameContext& /*ctx*/)
{
    ApplyShaderPipeline(dc, vertexShader.Get(), pixelShader.Get(), inputLayout.Get());
}


void MaterialShader::Update(ID3D11DeviceContext* dc,
    const Model::Mesh& mesh,
    const MaterialData* overrideMaterial)
{
    // overrideMaterial が非 null ならそちらを優先する。
    // mesh.material を書き換えない（const_cast 廃止）。
    const MaterialData* mat = overrideMaterial
        ? overrideMaterial : (mesh.material && mesh.material->data ? mesh.material->data.get() : nullptr);

    if (mat) UpdateMaterialProperties(dc, *mat);
}

void MaterialShader::End(ID3D11DeviceContext* dc, const RenderServices& /*s*/)
{
    UnbindResources(dc);
}


void MaterialShader::CheckAndReload(ID3D11Device* device)
{
#if defined(_DEBUG) || defined(ENABLE_EDITOR) // 本番ビルドでは走らせない
    auto currentVsTime = GetLastWriteTime(_vsHlslPath);
    auto currentPsTime = GetLastWriteTime(_psHlslPath);

    bool needReload = false;

    // --- ピクセルシェーダーの更新検知 ---
    if (currentPsTime > _psLastWriteTime) {
        _psLastWriteTime = currentPsTime;
        Microsoft::WRL::ComPtr<ID3D11PixelShader> newPixelShader;
        Microsoft::WRL::ComPtr<ID3DBlob>          newBlob;

        // HLSLを動的コンパイル (※あなたの GpuResourceUtils の実装に合わせてください)
        if (GpuResourceUtils::CompilePixelShader(device, _psHlslPath, "main", "ps_5_0",
            newPixelShader.GetAddressOf(), newBlob.GetAddressOf()))
        {
            pixelShader = newPixelShader;

            // ★超重要：リフレクション情報をゼロから作り直す
            ResetReflection();
            _reflection.LoadFromBytecode(newBlob->GetBufferPointer(), newBlob->GetBufferSize());

            // 定数バッファの再構築
            const auto& cbs = _reflection.GetConstantBuffers();
            for (const auto& cbInfo : cbs) {
                if (cbInfo.bindPoint < 8) { // システム予約スロット(8以降)以外を生成
                    _constantBuffers[cbInfo.name].Create(device, cbInfo);
                }
            }
            needReload = true;
            CCL_LOG_SUCCESS(LogCategory::Core, "Hot Reloaded: %ls", _psHlslPath.c_str());
        }
    }

    // --- 頂点シェーダーの更新検知 ---
    // (VSも必要であれば同様にCompileVertexShaderを実装してください)

#endif
}