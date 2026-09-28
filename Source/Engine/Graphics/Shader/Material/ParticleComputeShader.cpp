#include "ParticleComputeShader.h"
#include "Engine/Graphics/RHI/GpuResourceUtils.h"
#include "Engine/Platform/Core/Logger.h"

ParticleComputeShader::ParticleComputeShader(ID3D11Device *device)
    : m_initCsPath(L"Shader/Compute/GPUParticle/initialize_particle_cs.hlsl"), m_updateCsPath(L"Shader/Compute/GPUParticle/integrate_particle_cs.hlsl")
{
    LoadShaders(device);
}

void ParticleComputeShader::LoadShaders(ID3D11Device *device)
{
#if defined(_DEBUG) || defined(ENABLE_EDITOR)
    // 開発中: HLSL をその場でコンパイルする（保存すると CheckAndReload ですぐに反映される）
    GpuResourceUtils::CompileComputeShader(device, m_initCsPath, "main", "cs_5_0", m_initCS.ReleaseAndGetAddressOf());
    GpuResourceUtils::CompileComputeShader(device, m_updateCsPath, "main", "cs_5_0", m_updateCS.ReleaseAndGetAddressOf());
#else
    // 製品: HLSL（Shader/ の下）は同梱しないので、コンパイル済みの .cso を読む
    GpuResourceUtils::LoadComputeShader(device, "Assets/Shader/initialize_particle_cs.cso", m_initCS.ReleaseAndGetAddressOf());
    GpuResourceUtils::LoadComputeShader(device, "Assets/Shader/integrate_particle_cs.cso", m_updateCS.ReleaseAndGetAddressOf());
#endif

    m_initCsTime   = GetLastWriteTime(m_initCsPath);
    m_updateCsTime = GetLastWriteTime(m_updateCsPath);
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

    if (curInit > m_initCsTime || curUpdate > m_updateCsTime)
    {
        LoadShaders(device);
        CCL_LOG_SUCCESS(LogCategory::Render, "Particle Compute Shaders Hot-Reloaded.");
    }
#endif
}