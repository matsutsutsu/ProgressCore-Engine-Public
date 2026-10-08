#include "ParticleComputeShader.h"
#include "Engine/Graphics/RHI/GpuResourceUtils.h"
#include "Engine/Platform/Core/Logger.h"

ParticleComputeShader::ParticleComputeShader(ID3D11Device *device)
    : m_initCsPath(L"Shader/Compute/GPUParticle/initialize_particle_cs.hlsl"), m_updateCsPath(L"Shader/Compute/GPUParticle/integrate_particle_cs.hlsl"), m_includePath(L"Shader/Compute/GPUParticle/particle.hlsli")
{
    LoadShaders(device);
}

bool ParticleComputeShader::LoadShaders(ID3D11Device *device)
{
    bool allOk = true;

#if defined(_DEBUG) || defined(ENABLE_EDITOR)
    // 開発中: HLSL をその場でコンパイルする（保存すると CheckAndReload ですぐに反映される）
    //   一度ローカルに受けて、成功したものだけ差し替える。メンバへ直接書くと
    //   ReleaseAndGetAddressOf が先に古いシェーダーを捨てるので、打ち間違い1つで
    //   null になり、直すまで全パーティクルが止まる（ParticleShader と同じ書き方）。
    Microsoft::WRL::ComPtr<ID3D11ComputeShader> initCS;
    Microsoft::WRL::ComPtr<ID3D11ComputeShader> updateCS;

    const bool initOk   = GpuResourceUtils::CompileComputeShader(device, m_initCsPath, "main", "cs_5_0", initCS.GetAddressOf());
    const bool updateOk = GpuResourceUtils::CompileComputeShader(device, m_updateCsPath, "main", "cs_5_0", updateCS.GetAddressOf());

    if (initOk) m_initCS = initCS;
    if (updateOk) m_updateCS = updateCS;
    allOk = initOk && updateOk;
#else
    // 製品: HLSL（Shader/ の下）は同梱しないので、コンパイル済みの .cso を読む
    GpuResourceUtils::LoadComputeShader(device, "Assets/Shader/initialize_particle_cs.cso", m_initCS.ReleaseAndGetAddressOf());
    GpuResourceUtils::LoadComputeShader(device, "Assets/Shader/integrate_particle_cs.cso", m_updateCS.ReleaseAndGetAddressOf());
#endif

    m_initCsTime   = GetLastWriteTime(m_initCsPath);
    m_updateCsTime = GetLastWriteTime(m_updateCsPath);
    m_includeTime  = GetLastWriteTime(m_includePath);
    return allOk;
}

void ParticleComputeShader::Begin(ID3D11DeviceContext *dc, const RenderServices &, const RenderFrameContext &)
{
}

void ParticleComputeShader::End(ID3D11DeviceContext *dc, const RenderServices &)
{
    ID3D11ComputeShader *nullCS = nullptr;
    dc->CSSetShader(nullCS, nullptr, 0);
}

void ParticleComputeShader::CheckAndReload(ID3D11Device *device)
{
#if defined(_DEBUG) || defined(ENABLE_EDITOR)
    auto curInit   = GetLastWriteTime(m_initCsPath);
    auto curUpdate = GetLastWriteTime(m_updateCsPath);
    auto curInc    = GetLastWriteTime(m_includePath);

    if (curInit > m_initCsTime || curUpdate > m_updateCsTime || curInc > m_includeTime)
    {
        if (LoadShaders(device))
            CCL_LOG_SUCCESS(LogCategory::Render, "Particle Compute Shaders Hot-Reloaded.");
        else
            CCL_LOG_ERROR(LogCategory::Render, "Particle Compute Shaders: コンパイルに失敗したシェーダーは前のまま使う（詳細は出力ウィンドウ）");
    }
#endif
}