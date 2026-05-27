#include "MaterialShader.h"
#include "Engine/Graphics/RHI/GpuResourceUtils.h"
#include "Engine/Graphics/Resource/ModelResource.h"
#include "Engine/Platform/Core/Logger.h"

MaterialShader::MaterialShader(ID3D11Device* device,
    const std::string& vsCsoPath, const std::wstring& vsHlslPath,
    const std::string& psCsoPath, const std::wstring& psHlslPath)
    : _vsHlslPath(vsHlslPath), _psHlslPath(psHlslPath)
{
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

std::filesystem::file_time_type MaterialShader::GetLastWriteTime(const std::wstring& path) {
    if (std::filesystem::exists(path)) {
        return std::filesystem::last_write_time(path);
    }
    return (std::filesystem::file_time_type::min)();
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