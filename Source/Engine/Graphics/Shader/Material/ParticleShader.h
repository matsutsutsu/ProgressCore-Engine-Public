// ParticleShader.h
#pragma once
#include "Engine/Graphics/Shader/Core/Shader.h"

// パーティクル描画専用のシェーダーラッパー（ホットリロード対応）
class ParticleShader : public Shader {
public:
    ParticleShader(ID3D11Device* device);
    ~ParticleShader() override = default;

    void Begin(ID3D11DeviceContext* dc, const RenderServices& s, const RenderFrameContext& ctx) override;
    void End(ID3D11DeviceContext* dc, const RenderServices& s) override;

    void CheckAndReload(ID3D11Device* device) override;

private:
    Microsoft::WRL::ComPtr<ID3D11VertexShader>   m_vs;
    Microsoft::WRL::ComPtr<ID3D11GeometryShader> m_gs;
    Microsoft::WRL::ComPtr<ID3D11PixelShader>    m_ps;

    std::wstring m_vsPath, m_gsPath, m_psPath;
    std::filesystem::file_time_type m_vsTime, m_gsTime, m_psTime;

    void LoadShaders(ID3D11Device* device);
};