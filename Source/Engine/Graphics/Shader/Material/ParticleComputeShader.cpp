#include "ParticleComputeShader.h"
#include "Engine/Graphics/RHI/GpuResourceUtils.h"
#include "Engine/Platform/Core/Logger.h"

ParticleComputeShader::ParticleComputeShader(ID3D11Device* device)
    : _initCsPath(L"Shader/Compute/GPUParticle/initialize_particle_cs.hlsl")
    , _updateCsPath(L"Shader/Compute/GPUParticle/integrate_particle_cs.hlsl")
{
    LoadShaders(device);
}

void ParticleComputeShader::LoadShaders(ID3D11Device* device)
{
    // GpuResourceUtils の CompileComputeShader を使用して動的コンパイル＆生成
    // ※ 既存の .cso 固定読み込みから、拡張した動的コンパイル経由に変更することで開発効率を極大化
    GpuResourceUtils::CompileComputeShader(device, _initCsPath, "main", "cs_5_0", _initCS.ReleaseAndGetAddressOf());
    GpuResourceUtils::CompileComputeShader(device, _updateCsPath, "main", "cs_5_0", _updateCS.ReleaseAndGetAddressOf());

    _initCsTime = GetLastWriteTime(_initCsPath);
    _updateCsTime = GetLastWriteTime(_updateCsPath);
}

void ParticleComputeShader::Begin(ID3D11DeviceContext* dc, const RenderServices&, const RenderFrameContext&)
{
    // Compute Shader の実行コンテキスト初期化が必要な場合はここに記述
}

void ParticleComputeShader::End(ID3D11DeviceContext* dc, const RenderServices&)
{
    // スロットのクリーンアップ
    ID3D11ComputeShader* nullCS = nullptr;
    dc->CSSetShader(nullCS, nullptr, 0);
}

void ParticleComputeShader::CheckAndReload(ID3D11Device* device)
{
#if defined(_DEBUG) || defined(ENABLE_EDITOR)
    auto curInit = GetLastWriteTime(_initCsPath);
    auto curUpdate = GetLastWriteTime(_updateCsPath);

    if (curInit > _initCsTime || curUpdate > _updateCsTime) {
        LoadShaders(device);
        // ホットリロード成功時に CCL_LOG_SUCCESS を使用
        CCL_LOG_SUCCESS(LogCategory::Render, "Particle Compute Shaders Hot-Reloaded.");
    }
#endif
}