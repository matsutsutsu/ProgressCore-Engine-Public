#pragma once
// ===========================================================================
// 概要: GPU パーティクルを描く頂点・ジオメトリ・ピクセルシェーダーの組。
//
// 責務:
//   - 起動時に3つの .cso を読み、Begin / End でバインドする・外す
//   - HLSL の更新時刻を見て、変わっていたら HLSL をその場でコンパイルし直す
//
// 備考: 粒子のバッファ・定数バッファ・テクスチャは ParticleRenderer がバインドする。
// ===========================================================================
#include "Engine/Graphics/Shader/Core/Shader.h"

class ParticleShader : public Shader
{
public:
    ParticleShader(ID3D11Device *device);
    ~ParticleShader() override = default;

    void Begin(ID3D11DeviceContext *dc, const RenderServices &s, const RenderFrameContext &ctx) override;
    void End(ID3D11DeviceContext *dc, const RenderServices &s) override;

    void CheckAndReload(ID3D11Device *device) override;

private:
    Microsoft::WRL::ComPtr<ID3D11VertexShader>   m_vs;
    Microsoft::WRL::ComPtr<ID3D11GeometryShader> m_gs;
    Microsoft::WRL::ComPtr<ID3D11PixelShader>    m_ps;

    std::wstring                    m_vsPath, m_gsPath, m_psPath; // 更新を見る HLSL
    std::filesystem::file_time_type m_vsTime, m_gsTime, m_psTime; // 前に見た更新時刻

    void LoadShaders(ID3D11Device *device);
    // HLSL をコンパイルし直す。失敗したシェーダーは前のまま
    void CompileShaders(ID3D11Device *device);
};