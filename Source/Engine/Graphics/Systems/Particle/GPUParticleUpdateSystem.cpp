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

GPUParticleUpdateSystem::~GPUParticleUpdateSystem() = default;

// =========================================================================
// BuildCbParticle — Desc + ランタイム値から CbParticle を組み立てる
//
// 設計:
//   - スタック上に 224 バイト作るだけ（軽量）。
//   - 権威データ (Desc) と毎フレーム変わるランタイム値を明確に分離して記述。
//   - 将来 CbParticle を Static/Dynamic に分割する際は、この関数を2つに分割する。
// =========================================================================
static GpuData::CbParticle BuildCbParticle(
    const ParticleEmitterDesc& desc,
    const TransformComponent& transform,
    float                      time,
    float                      dt,
    float                      burstTrigger)
{
    GpuData::CbParticle cb = {};

    // ---- Desc 由来（権威データ） ----
    cb.emission_local_offset = {
        desc.localOffset.x, desc.localOffset.y, desc.localOffset.z,
        desc.offsetY            // .w に offsetY を収納 (旧 particle_offset_y)
    };
    cb.emission_offset = desc.emissionOffset;
    cb.emission_size = desc.emissionSize;
    cb.emission_cone_angle = desc.coneAngle;
    cb.emission_speed = desc.emissionSpeed;
    cb.emission_angular_speed = desc.emissionAngularSpeed;
    cb.lifespan = desc.lifespan;
    cb.spawn_delay = desc.spawnDelay;
    cb.fade_duration = desc.fadeDuration;
    cb.manual_color = desc.manualColor;
    cb.noise_scale = desc.noiseScale;
    cb.noise_strength = desc.noiseStrength;
    cb.gravity = desc.gravity;
    cb.velocity_stretch = desc.velocityStretch;
    cb.uv_scroll_speed = desc.uvScrollSpeed;
    cb.sprite_anim_mode = desc.spriteAnimMode;
    cb.render_mode = desc.renderMode;
    cb.sprite_sheet_grid = desc.spriteSheetGrid;
    cb.particle_scale = desc.particleScale;
    cb.color_mode = desc.colorMode;
    cb.max_particle_count = desc.maxParticleCount;
    cb.type = desc.type;
    cb.emission_mode = desc.emissionMode;
    cb.burst_count = desc.burstCount;
    cb.emission_stretch = desc.emissionStretch;

    // ---- ランタイム由来（毎フレーム計算） ----
    cb.world_position = transform.GetWorldPosition();
    cb.emission_rotation = transform.rotation;
    cb.time = time;
    cb.delta_time = dt;
    cb.burst_trigger = burstTrigger;

    return cb;
}

// =========================================================================
void GPUParticleUpdateSystem::InitializeParticle(ID3D11Device* device)
{
    _device = device;

    _computeShader = std::make_unique<ParticleComputeShader>(device);

    D3D11_SAMPLER_DESC sampDesc = {};
    sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
    sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
    sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    device->CreateSamplerState(&sampDesc, _sampler.GetAddressOf());

    CreateNoiseTexture(device);
}

void GPUParticleUpdateSystem::Update(float dt)
{
    if (!_isLoaded) {
        InitializeParticle(Graphics::Instance().GetDevice());
        _isLoaded = true;
    }

    ID3D11DeviceContext* dc = Graphics::Instance().GetDeviceContext();
    if (!dc || !_computeShader) return;

    _computeShader->CheckAndReload(_device);

    if (!_computeShader->GetInitCS() || !_computeShader->GetUpdateCS()) return;

    // CS の UAV バインド前に前フレームの SRV スロットをクリア
    // （同一リソースへの SRV/UAV 同時バインド禁止のため）
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
            if (auto* pooled = _world->GetComponent<PooledParticleComponent>(id)) {
                if (pooled->isSleeping) return;
            }

            if (!particle.isInitialized) {
                RebuildParticleBuffer(particle);
            }

            GpuParticleBufferSet* buffers =
                ResourceManager::Instance().GetParticleBuffer(particle.computeBuffer);
            if (!buffers || !buffers->particleUAV) return;

            if (particle.needsGpuInit) {
                DispatchInitCS(dc, _computeShader.get(), particle, transform, buffers);
            }
            else {
                DispatchUpdateCS(dc, _computeShader.get(), particle, transform, buffers, dt);
            }
        });

    dc->CSSetShader(nullptr, nullptr, 0);
}

// =========================================================================
// [WARNING] フレームスパイク注意:
//   内部で CreateBuffer を呼ぶため重い処理。
//   大量同時スポーン時はカクつく。将来 ResourceManager 側で
//   バッファプール（CreateBuffer → 貸し出し）化を推奨。
// =========================================================================
void GPUParticleUpdateSystem::RebuildParticleBuffer(GPUParticleComponent& particle)
{
    if (particle.computeBuffer.IsValid()) {
        ResourceManager::Instance().UnloadParticleBuffer(particle.computeBuffer);
        particle.computeBuffer = ParticleBufferHandle{};
    }

    ID3D11Device* device = Graphics::Instance().GetDevice();
    // ★ desc.maxParticleCount が唯一の権威（maxParticles メンバ廃止済み）
    if (!device || particle.desc.maxParticleCount <= 0) return;

    particle.computeBuffer = ResourceManager::Instance().CreateParticleBuffer();
    GpuParticleBufferSet* buffers =
        ResourceManager::Instance().GetParticleBuffer(particle.computeBuffer);
    if (!buffers) return;

    const UINT count = static_cast<UINT>(particle.desc.maxParticleCount);

    // StructuredBuffer（粒子配列）
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

    // 定数バッファ (CbParticle, 224 bytes / 16B 整列済み)
    D3D11_BUFFER_DESC cbDesc = {};
    cbDesc.ByteWidth = (sizeof(GpuData::CbParticle) + 15) & ~15u;
    cbDesc.Usage = D3D11_USAGE_DEFAULT;
    cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    device->CreateBuffer(&cbDesc, nullptr, &buffers->constantBuffer);

    particle.isInitialized = true;
    particle.needsGpuInit = true;
    particle.time = 0.0f; // 新規バッファ作成時は時間リセット
}

// =========================================================================
/// @brief 1エミッタ分の初期化 CS を Dispatch する。
///        終了後 needsGpuInit を false にする。
// =========================================================================
void GPUParticleUpdateSystem::DispatchInitCS(
    ID3D11DeviceContext* dc,
    ParticleComputeShader* cs,
    GPUParticleComponent& particle,
    const TransformComponent& transform,
    GpuParticleBufferSet* buffers)
{
    if (!cs || !buffers) return;

    // InitCS では delta_time は厳密に不要（初期化なので 0.016f をデフォルトに）
    GpuData::CbParticle cb = BuildCbParticle(
        particle.desc, transform,
        particle.time, 0.016f, particle.burstTrigger);

    if (buffers->constantBuffer) {
        dc->UpdateSubresource(buffers->constantBuffer, 0, nullptr, &cb, 0, 0);
        ID3D11Buffer* cbs[] = { buffers->constantBuffer };
        dc->CSSetConstantBuffers(SLOT_CB_PARTICLE, 1, cbs);
    }

    dc->CSSetShader(cs->GetInitCS(), nullptr, 0);

    ID3D11UnorderedAccessView* uavs[] = { buffers->particleUAV };
    dc->CSSetUnorderedAccessViews(SLOT_UAV_PARTICLE_BUF, 1, uavs, nullptr);

    const UINT groups = (static_cast<UINT>(particle.desc.maxParticleCount) + 15) / 16;
    dc->Dispatch(groups, 1, 1);

    ID3D11UnorderedAccessView* nullUAV[] = { nullptr };
    dc->CSSetUnorderedAccessViews(SLOT_UAV_PARTICLE_BUF, 1, nullUAV, nullptr);

    particle.needsGpuInit = false;
}

// =========================================================================
/// @brief 1エミッタ分の更新 CS を Dispatch する。
///        CB 更新・ランプ / ノイズ / サンプラーをバインドする。
// =========================================================================
void GPUParticleUpdateSystem::DispatchUpdateCS(
    ID3D11DeviceContext* dc,
    ParticleComputeShader* cs,
    GPUParticleComponent& particle,
    const TransformComponent& transform,
    GpuParticleBufferSet* buffers,
    float dt)
{
    if (!cs || !buffers) return;

    // time を先に更新してから CB に詰める
    particle.time += dt;

    GpuData::CbParticle cb = BuildCbParticle(
        particle.desc, transform,
        particle.time, dt, particle.burstTrigger);

    if (buffers->constantBuffer) {
        dc->UpdateSubresource(buffers->constantBuffer, 0, nullptr, &cb, 0, 0);
        ID3D11Buffer* cbs[] = { buffers->constantBuffer };
        dc->CSSetConstantBuffers(SLOT_CB_PARTICLE, 1, cbs);
    }

    dc->CSSetShader(cs->GetUpdateCS(), nullptr, 0);

    // カラーランプ (t1)
    if (ID3D11ShaderResourceView* rampSRV = ResourceManager::Instance().GetTexture(particle.colorRamp)) {
        ID3D11ShaderResourceView* v[] = { rampSRV };
        dc->CSSetShaderResources(SLOT_SRV_MAT_TEX1, 1, v);
    }
    // 3D ノイズ (t2)
    if (_noiseSRV) {
        ID3D11ShaderResourceView* v[] = { _noiseSRV.Get() };
        dc->CSSetShaderResources(SLOT_SRV_MAT_TEX2, 1, v);
    }
    // サンプラー (s1)
    if (_sampler) {
        ID3D11SamplerState* s[] = { _sampler.Get() };
        dc->CSSetSamplers(SLOT_SMP_LINEAR, 1, s);
    }

    ID3D11UnorderedAccessView* uavs[] = { buffers->particleUAV };
    dc->CSSetUnorderedAccessViews(SLOT_UAV_PARTICLE_BUF, 1, uavs, nullptr);

    const UINT groups = (static_cast<UINT>(particle.desc.maxParticleCount) + 15) / 16;
    dc->Dispatch(groups, 1, 1);

    // 後始末
    ID3D11UnorderedAccessView* nullUAV[] = { nullptr };
    dc->CSSetUnorderedAccessViews(SLOT_UAV_PARTICLE_BUF, 1, nullUAV, nullptr);
    ID3D11ShaderResourceView* nullSRV[] = { nullptr };
    dc->CSSetShaderResources(SLOT_SRV_MAT_TEX1, 1, nullSRV);
    dc->CSSetShaderResources(SLOT_SRV_MAT_TEX2, 1, nullSRV);

    // バーストは1フレームで消費
    if (particle.desc.emissionMode == 1) {
        particle.burstTrigger = 0.0f;
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

REGISTER_RENDER_SYSTEM(GPUParticleUpdateSystem, Priority::RenderStage::R07_PreMain);