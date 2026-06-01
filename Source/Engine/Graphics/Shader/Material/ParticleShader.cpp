// ParticleShader.cpp
#include "ParticleShader.h"
#include "Engine/Graphics/RHI/GpuResourceUtils.h"

ParticleShader::ParticleShader(ID3D11Device* device)
    : _vsPath(L"Shader/Compute/GPUParticle/particle_vs.hlsl")
    , _gsPath(L"Shader/Compute/GPUParticle/particle_gs.hlsl")
    , _psPath(L"Shader/Compute/GPUParticle/particle_ps.hlsl")
{
    LoadShaders(device);
}

void ParticleShader::LoadShaders(ID3D11Device* device) {
    // ※ GpuResourceUtils等でCSOをロード、またはHLSLからコンパイルする処理
    GpuResourceUtils::LoadVertexShader(device, "Assets/Shader/particle_vs.cso", nullptr, 0, nullptr, _vs.GetAddressOf());
    GpuResourceUtils::LoadGeometryShader(device, "Assets/Shader/particle_gs.cso", _gs.GetAddressOf());
    GpuResourceUtils::LoadPixelShader(device, "Assets/Shader/particle_ps.cso", _ps.GetAddressOf());

    _vsTime = GetLastWriteTime(_vsPath);
    _gsTime = GetLastWriteTime(_gsPath);
    _psTime = GetLastWriteTime(_psPath);
}

void ParticleShader::Begin(ID3D11DeviceContext* dc, const RenderServices& s, const RenderFrameContext& ctx) {
    dc->VSSetShader(_vs.Get(), nullptr, 0);
    dc->GSSetShader(_gs.Get(), nullptr, 0);
    dc->PSSetShader(_ps.Get(), nullptr, 0);
}

void ParticleShader::End(ID3D11DeviceContext* dc, const RenderServices& s) {
    dc->VSSetShader(nullptr, nullptr, 0);
    dc->GSSetShader(nullptr, nullptr, 0);
    dc->PSSetShader(nullptr, nullptr, 0);
}

void ParticleShader::CheckAndReload(ID3D11Device* device) {
#if defined(_DEBUG) || defined(ENABLE_EDITOR)
    auto curVs = GetLastWriteTime(_vsPath);
    auto curGs = GetLastWriteTime(_gsPath);
    auto curPs = GetLastWriteTime(_psPath);

    if (curVs > _vsTime || curGs > _gsTime || curPs > _psTime) {
        LoadShaders(device);
        OutputDebugStringA("[HotReload] Particle Shaders reloaded.\n");
    }
#endif
}