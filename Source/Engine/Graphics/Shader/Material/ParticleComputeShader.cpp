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
    // 開発時：HLSLソースから動的コンパイル。編集→CheckAndReload で即ホットリロードできる。
    GpuResourceUtils::CompileComputeShader(device, m_initCsPath, "main", "cs_5_0", m_initCS.ReleaseAndGetAddressOf());
    GpuResourceUtils::CompileComputeShader(device, m_updateCsPath, "main", "cs_5_0", m_updateCS.ReleaseAndGetAddressOf());
#else
    // 製品(Shipping)：同梱済みのコンパイル済み .cso を読む。
    // HLSLソース(Shader/ 配下)は同梱しないため、動的コンパイルでは失敗する。
    // 描画側 ParticleShader が Assets/Shader/*.cso を読むのと同じ方式に揃える。
    GpuResourceUtils::LoadComputeShader(device, "Assets/Shader/initialize_particle_cs.cso", m_initCS.ReleaseAndGetAddressOf());
    GpuResourceUtils::LoadComputeShader(device, "Assets/Shader/integrate_particle_cs.cso", m_updateCS.ReleaseAndGetAddressOf());
#endif

    m_initCsTime   = GetLastWriteTime(m_initCsPath);
    m_updateCsTime = GetLastWriteTime(m_updateCsPath);
}

void ParticleComputeShader::Begin(ID3D11DeviceContext *dc, const RenderServices &, const RenderFrameContext &)
{
    // Compute Shader の実行コンテキスト初期化が必要な場合はここに記述
}

void ParticleComputeShader::End(ID3D11DeviceContext *dc, const RenderServices &)
{
    // スロットのクリーンアップ
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
        // ホットリロード成功時に CCL_LOG_SUCCESS を使用
        CCL_LOG_SUCCESS(LogCategory::Render, "Particle Compute Shaders Hot-Reloaded.");
    }
#endif
}