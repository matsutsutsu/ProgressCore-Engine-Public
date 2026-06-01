#include "GPUParticleUpdateSystem.h"
#include "Engine/Graphics/RHI/Graphics.h"
#include "Engine/Graphics/Resource/ResourceManager.h"
#include "Engine/Graphics/Shader/ShaderResources.h"
#include "Engine/Graphics/Shader/ShaderShared.h"
#include "Engine/Graphics/Shader/Material/ParticleComputeShader.h"
#include "Game/Utils/PooledParticleComponent.h"

#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Game/Core/SystemPriority.h"

#include <random>

// unique_ptr<ParticleComputeShader>（前方宣言型）のため、完全型が見えるここで定義
GPUParticleUpdateSystem::~GPUParticleUpdateSystem() = default;

void GPUParticleUpdateSystem::InitializeParticle(ID3D11Device* device)
{
    _device = device;

    // Compute Shader 管理クラス（HotReload対応）
    _computeShader = std::make_unique<ParticleComputeShader>(device);

    // サンプラー
    D3D11_SAMPLER_DESC sampDesc = {};
    sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
    sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
    sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    device->CreateSamplerState(&sampDesc, _sampler.GetAddressOf());

    // 3Dノイズ（※ResourceManagerへ移管済みなら、後述の通りそちらから取得に差し替え推奨）
    CreateNoiseTexture(device);
}

void GPUParticleUpdateSystem::Update(float dt)
{
    // --- 初回初期化 ---
    if (!_isLoaded) {
        InitializeParticle(Graphics::Instance().GetDevice());
        _isLoaded = true;   // ★これが抜けていたのが最大の原因
    }

    ID3D11DeviceContext* dc = Graphics::Instance().GetDeviceContext();
    if (!dc || !_computeShader) return;

    // HLSL更新検知（ShaderRegistry::UpdateHotReload を中央で回しているなら削除可）
    _computeShader->CheckAndReload(_device);

    // CSが両方そろっていなければ何もしない（コンパイル失敗時の保険）
    if (!_computeShader->GetInitCS() || !_computeShader->GetUpdateCS()) return;

    // 前フレームの particleSRV が VS/GS/PS に残っていると、
    // CS の UAV バインドが D3D 側で弾かれる（同一リソースのSRV/UAV同時バインド禁止）。
    ID3D11ShaderResourceView* nullSRVs[16] = { nullptr };
    dc->VSSetShaderResources(0, 16, nullSRVs);
    dc->GSSetShaderResources(0, 16, nullSRVs);
    dc->PSSetShaderResources(0, 16, nullSRVs);
    dc->CSSetShaderResources(0, 16, nullSRVs);

    ForEachWithID(
        [&](CCL::ECS::EntityID id,
            GPUParticleComponent& particle,
            const TransformComponent& transform)
        {
            // スリープ中（プール待機）はスキップ
            if (auto* pooled = _world->GetComponent<PooledParticleComponent>(id)) {
                if (pooled->isSleeping) return;
            }

            // リソース未作成なら生成
            if (!particle.isInitialized) {
                RebuildParticleBuffer(particle);
            }

            GpuParticleBufferSet* buffers =
                ResourceManager::Instance().GetParticleBuffer(particle.computeBuffer);
            if (!buffers || !buffers->particleUAV) return;

            // GPU初期化が必要なら Init、それ以外は Update
            if (particle.needsGpuInit) {
                DispatchInitCS(dc, _computeShader.get(), particle, transform, buffers);
            }
            else {
                DispatchUpdateCS(dc, _computeShader.get(), particle, transform, buffers, dt);
            }
        });

    // 後始末
    dc->CSSetShader(nullptr, nullptr, 0);
}

// =================================================================================
// [WARNING] フレームスパイク注意：内部で CreateBuffer を呼ぶため重い。
// 大量同時スポーン時はカクつく。将来 ResourceManager 側でバッファプール化を推奨。
// =================================================================================
void GPUParticleUpdateSystem::RebuildParticleBuffer(GPUParticleComponent& particle)
{
    if (particle.computeBuffer.IsValid()) {
        ResourceManager::Instance().UnloadParticleBuffer(particle.computeBuffer);
        particle.computeBuffer = ParticleBufferHandle{};
    }

    ID3D11Device* device = Graphics::Instance().GetDevice();
    if (!device || particle.config.max_particle_count <= 0) return;

    particle.computeBuffer = ResourceManager::Instance().CreateParticleBuffer();
    GpuParticleBufferSet* buffers =
        ResourceManager::Instance().GetParticleBuffer(particle.computeBuffer);
    if (!buffers) return;

    const UINT count = static_cast<UINT>(particle.config.max_particle_count);

    D3D11_BUFFER_DESC sbDesc = {};
    sbDesc.ByteWidth = sizeof(GpuData::GpuParticle) * count;
    sbDesc.Usage = D3D11_USAGE_DEFAULT;
    sbDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
    sbDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    sbDesc.StructureByteStride = sizeof(GpuData::GpuParticle);
    device->CreateBuffer(&sbDesc, nullptr, &buffers->particleBuffer);

    D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.Format = DXGI_FORMAT_UNKNOWN;
    uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
    uavDesc.Buffer.NumElements = count;
    device->CreateUnorderedAccessView(buffers->particleBuffer, &uavDesc, &buffers->particleUAV);

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = DXGI_FORMAT_UNKNOWN;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    srvDesc.Buffer.NumElements = count;
    device->CreateShaderResourceView(buffers->particleBuffer, &srvDesc, &buffers->particleSRV);

    D3D11_BUFFER_DESC cbDesc = {};
    cbDesc.ByteWidth = (sizeof(GpuData::CbParticle) + 15) & ~15u; // 16B切り上げ
    cbDesc.Usage = D3D11_USAGE_DEFAULT;
    cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    device->CreateBuffer(&cbDesc, nullptr, &buffers->constantBuffer);

    particle.isInitialized = true;
    particle.needsGpuInit = true;
}

void GPUParticleUpdateSystem::DispatchInitCS(ID3D11DeviceContext* dc, ParticleComputeShader* cs,
    GPUParticleComponent& particle, const TransformComponent& transform,
    GpuParticleBufferSet* buffers)
{
    if (!cs || !buffers) return;

    particle.config.world_position = transform.GetWorldPosition();
    particle.config.emission_rotation = transform.rotation;
    if (particle.config.delta_time == 0.0f) particle.config.delta_time = 0.016f;

    if (buffers->constantBuffer) {
        dc->UpdateSubresource(buffers->constantBuffer, 0, nullptr, &particle.config, 0, 0);
        ID3D11Buffer* cbs[] = { buffers->constantBuffer };
        dc->CSSetConstantBuffers(SLOT_CB_PARTICLE, 1, cbs);
    }

    dc->CSSetShader(cs->GetInitCS(), nullptr, 0);

    ID3D11UnorderedAccessView* uavs[] = { buffers->particleUAV };
    dc->CSSetUnorderedAccessViews(SLOT_UAV_PARTICLE_BUF, 1, uavs, nullptr);

    const UINT groups = (static_cast<UINT>(particle.config.max_particle_count) + 15) / 16;
    dc->Dispatch(groups, 1, 1);

    ID3D11UnorderedAccessView* nullUAV[] = { nullptr };
    dc->CSSetUnorderedAccessViews(SLOT_UAV_PARTICLE_BUF, 1, nullUAV, nullptr);

    particle.needsGpuInit = false;
}

void GPUParticleUpdateSystem::DispatchUpdateCS(ID3D11DeviceContext* dc, ParticleComputeShader* cs,
    GPUParticleComponent& particle, const TransformComponent& transform,
    GpuParticleBufferSet* buffers, float dt)
{
    if (!cs || !buffers) return;

    particle.config.world_position = transform.GetWorldPosition();
    particle.config.delta_time = dt;
    particle.config.time += dt;

    if (buffers->constantBuffer) {
        dc->UpdateSubresource(buffers->constantBuffer, 0, nullptr, &particle.config, 0, 0);
        ID3D11Buffer* cbs[] = { buffers->constantBuffer };
        dc->CSSetConstantBuffers(SLOT_CB_PARTICLE, 1, cbs);
    }

    dc->CSSetShader(cs->GetUpdateCS(), nullptr, 0);

    // カラーランプ(t1)
    if (ID3D11ShaderResourceView* rampSRV = ResourceManager::Instance().GetTexture(particle.colorRamp)) {
        ID3D11ShaderResourceView* v[] = { rampSRV };
        dc->CSSetShaderResources(SLOT_SRV_MAT_TEX1, 1, v);
    }
    // 3Dノイズ(t2)
    if (_noiseSRV) {
        ID3D11ShaderResourceView* v[] = { _noiseSRV.Get() };
        dc->CSSetShaderResources(SLOT_SRV_MAT_TEX2, 1, v);
    }
    // サンプラー(s1)
    if (_sampler) {
        ID3D11SamplerState* s[] = { _sampler.Get() };
        dc->CSSetSamplers(SLOT_SMP_LINEAR, 1, s);
    }

    ID3D11UnorderedAccessView* uavs[] = { buffers->particleUAV };
    dc->CSSetUnorderedAccessViews(SLOT_UAV_PARTICLE_BUF, 1, uavs, nullptr);

    const UINT groups = (static_cast<UINT>(particle.config.max_particle_count) + 15) / 16;
    dc->Dispatch(groups, 1, 1);

    // 後始末（UAV/対象SRVを外す）
    ID3D11UnorderedAccessView* nullUAV[] = { nullptr };
    dc->CSSetUnorderedAccessViews(SLOT_UAV_PARTICLE_BUF, 1, nullUAV, nullptr);
    ID3D11ShaderResourceView* nullSRV[] = { nullptr };
    dc->CSSetShaderResources(SLOT_SRV_MAT_TEX1, 1, nullSRV);
    dc->CSSetShaderResources(SLOT_SRV_MAT_TEX2, 1, nullSRV);

    // バーストは1フレームで消費
    if (particle.config.emission_mode == 1) {
        particle.config.burst_trigger = 0.0f;
    }
}

void GPUParticleUpdateSystem::CreateNoiseTexture(ID3D11Device* device)
{
    const int size = 32;
    std::vector<float> noiseData(static_cast<size_t>(size) * size * size * 4);

    std::mt19937 gen(12345);
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);
    for (int i = 0; i < size * size * size; ++i) {
        noiseData[i * 4 + 0] = dist(gen);
        noiseData[i * 4 + 1] = dist(gen);
        noiseData[i * 4 + 2] = dist(gen);
        noiseData[i * 4 + 3] = 0.0f;
    }

    D3D11_TEXTURE3D_DESC desc = {};
    desc.Width = size; desc.Height = size; desc.Depth = size;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA initData = {};
    initData.pSysMem = noiseData.data();
    initData.SysMemPitch = size * sizeof(float) * 4;
    initData.SysMemSlicePitch = size * size * sizeof(float) * 4;

    device->CreateTexture3D(&desc, &initData, _noiseTexture.GetAddressOf());
    device->CreateShaderResourceView(_noiseTexture.Get(), nullptr, _noiseSRV.GetAddressOf());
}

REGISTER_RENDER_SYSTEM(GPUParticleUpdateSystem, Priority::RenderStage::R08_Main);