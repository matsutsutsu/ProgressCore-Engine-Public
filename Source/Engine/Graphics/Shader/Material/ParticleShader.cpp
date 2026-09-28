#include "ParticleShader.h"
#include "Engine/Graphics/RHI/GpuResourceUtils.h"
#include "Engine/Platform/Core/Logger.h"

ParticleShader::ParticleShader(ID3D11Device *device)
    : m_vsPath(L"Shader/Compute/GPUParticle/particle_vs.hlsl"), m_gsPath(L"Shader/Compute/GPUParticle/particle_gs.hlsl"), m_psPath(L"Shader/Compute/GPUParticle/particle_ps.hlsl")
{
    LoadShaders(device);
}

void ParticleShader::LoadShaders(ID3D11Device *device)
{
    GpuResourceUtils::LoadVertexShader(device, "Assets/Shader/particle_vs.cso", nullptr, 0, nullptr, m_vs.GetAddressOf());
    GpuResourceUtils::LoadGeometryShader(device, "Assets/Shader/particle_gs.cso", m_gs.GetAddressOf());
    GpuResourceUtils::LoadPixelShader(device, "Assets/Shader/particle_ps.cso", m_ps.GetAddressOf());

    m_vsTime = GetLastWriteTime(m_vsPath);
    m_gsTime = GetLastWriteTime(m_gsPath);
    m_psTime = GetLastWriteTime(m_psPath);
}

void ParticleShader::Begin(ID3D11DeviceContext *dc, const RenderServices &s, const RenderFrameContext &ctx)
{
    dc->VSSetShader(m_vs.Get(), nullptr, 0);
    dc->GSSetShader(m_gs.Get(), nullptr, 0);
    dc->PSSetShader(m_ps.Get(), nullptr, 0);
}

void ParticleShader::End(ID3D11DeviceContext *dc, const RenderServices &s)
{
    dc->VSSetShader(nullptr, nullptr, 0);
    dc->GSSetShader(nullptr, nullptr, 0);
    dc->PSSetShader(nullptr, nullptr, 0);
}

void ParticleShader::CheckAndReload(ID3D11Device *device)
{
#if defined(_DEBUG) || defined(ENABLE_EDITOR)
    auto curVs = GetLastWriteTime(m_vsPath);
    auto curGs = GetLastWriteTime(m_gsPath);
    auto curPs = GetLastWriteTime(m_psPath);

    if (curVs > m_vsTime || curGs > m_gsTime || curPs > m_psTime)
    {
        m_vsTime = curVs;
        m_gsTime = curGs;
        m_psTime = curPs;
        CompileShaders(device);
    }
#endif
}

void ParticleShader::CompileShaders(ID3D11Device *device)
{
    Microsoft::WRL::ComPtr<ID3D11VertexShader>   vs;
    Microsoft::WRL::ComPtr<ID3D11GeometryShader> gs;
    Microsoft::WRL::ComPtr<ID3D11PixelShader>    ps;

    // 粒子のバッファから SV_VertexID で読むので、入力レイアウトは作らない
    const bool vsOk = GpuResourceUtils::CompileVertexShader(device, m_vsPath, "main", "vs_5_0", vs.GetAddressOf());
    const bool gsOk = GpuResourceUtils::CompileGeometryShader(device, m_gsPath, "main", "gs_5_0", gs.GetAddressOf());
    const bool psOk = GpuResourceUtils::CompilePixelShader(device, m_psPath, "main", "ps_5_0", ps.GetAddressOf());

    if (vsOk) m_vs = vs;
    if (gsOk) m_gs = gs;
    if (psOk) m_ps = ps;

    if (vsOk && gsOk && psOk)
        CCL_LOG_SUCCESS(LogCategory::Render, "Particle Shaders Hot-Reloaded.");
    else
        CCL_LOG_ERROR(LogCategory::Render, "Particle Shaders: コンパイルに失敗したシェーダーは前のまま使う（詳細は出力ウィンドウ）");
}