#include "MaterialShader.h"
#include "Engine/Graphics/RHI/GpuResourceUtils.h"
#include "Engine/Graphics/Resource/ModelResource.h"
#include "Engine/Platform/Core/Logger.h"

MaterialShader::MaterialShader(ID3D11Device      *device,
                               const std::string &vsCsoPath, const std::wstring &vsHlslPath,
                               const std::string &psCsoPath, const std::wstring &psHlslPath,
                               const PipelineStateDesc *psoDesc)
    : m_vsHlslPath(vsHlslPath), m_psHlslPath(psHlslPath)
{
    // JSON で PSO 指定があれば固める（無ければパス既定に委ねる）
    if (psoDesc)
    {
        m_pso    = PipelineState(*psoDesc);
        m_hasPso = true;
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
    // overrideMaterial が非 null ならそちらを優先する。
    // mesh.material を書き換えない（const_cast 廃止）。
    const MaterialData *baseMat = (mesh.material && mesh.material->data) ? mesh.material->data.get() : nullptr;
    const MaterialData *mat     = overrideMaterial ? overrideMaterial : baseMat;

    // override 使用時、テクスチャが欠けているスロットはモデル本来の材質(baseMat)で補う。
    //   （シーン保存で失われた assetPath 無しのモデル埋め込みテクスチャ対策）
    const MaterialData *fallback = (overrideMaterial && baseMat != overrideMaterial) ? baseMat : nullptr;

    if (mat) UpdateMaterialProperties(dc, *mat, fallback);
}

void MaterialShader::End(ID3D11DeviceContext *dc, const RenderServices & /*s*/)
{
    UnbindResources(dc);
}

void MaterialShader::CheckAndReload(ID3D11Device *device)
{
#if defined(_DEBUG) || defined(ENABLE_EDITOR) // 本番ビルドでは走らせない
    auto currentVsTime = GetLastWriteTime(m_vsHlslPath);
    auto currentPsTime = GetLastWriteTime(m_psHlslPath);

    bool needReload = false;

    // --- ピクセルシェーダーの更新検知 ---
    if (currentPsTime > m_psLastWriteTime)
    {
        m_psLastWriteTime = currentPsTime;
        Microsoft::WRL::ComPtr<ID3D11PixelShader> newPixelShader;
        Microsoft::WRL::ComPtr<ID3DBlob>          newBlob;

        // HLSL をその場でコンパイルする（ホットリロード用。失敗時は旧シェーダを維持）
        if (GpuResourceUtils::CompilePixelShader(device, m_psHlslPath, "main", "ps_5_0",
                                                 newPixelShader.GetAddressOf(), newBlob.GetAddressOf()))
        {
            pixelShader = newPixelShader;

            // 超重要：リフレクション情報をゼロから作り直す
            ResetReflection();
            m_reflection.LoadFromBytecode(newBlob->GetBufferPointer(), newBlob->GetBufferSize());

            // 定数バッファの再構築
            const auto &cbs = m_reflection.GetConstantBuffers();
            for (const auto &cbInfo : cbs)
            {
                if (cbInfo.bindPoint < 8)
                { // システム予約スロット(8以降)以外を生成
                    m_constantBuffers[cbInfo.name].Create(device, cbInfo);
                }
            }
            needReload = true;
            CCL_LOG_SUCCESS(LogCategory::Core, "Hot Reloaded: %ls", m_psHlslPath.c_str());
        }
    }

    // --- 頂点シェーダーの更新検知 ---
    // (VSも必要であれば同様にCompileVertexShaderを実装してください)

#endif
}