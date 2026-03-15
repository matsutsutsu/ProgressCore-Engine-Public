#include "GPUParticleUpdateSystem.h"
#include "Engine/Graphics/Core/Graphics.h"
#include "Engine/Graphics/Shader/ShaderResources.h"
#include <d3dcompiler.h>
#include <random>

#include "Game/Utils/PooledParticleComponent.h"
#include "Game/Utils/PooledBulletComponent.h"

// システムの実行順序の定義ヘッダー
#include "ECS/System/CCL_SystemRegistry.h"
#include "Game/Core/SystemPriority.h"

#pragma comment(lib, "d3dcompiler.lib")

void GPUParticleUpdateSystem::InitializeParticle(ID3D11Device *device)
{
    // 1. シェーダーロード
    ID3DBlob *blob = nullptr;

    // Init CS
    D3DReadFileToBlob(L"Assets/Shader/initialize_particle_cs.cso", &blob);
    if (blob) {
        device->CreateComputeShader(
            blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, initCS.GetAddressOf());
        blob->Release();
    }

    // Update CS
    blob = nullptr;
    D3DReadFileToBlob(L"Assets/Shader/integrate_particle_cs.cso", &blob);
    if (blob) {
        device->CreateComputeShader(
            blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, updateCS.GetAddressOf());
        blob->Release();
    }
    
    // ★追加: ノイズテクスチャ作成
    CreateNoiseTexture(device);

    // ★追加: サンプラーの作成
    D3D11_SAMPLER_DESC sampDesc = {};
    sampDesc.Filter             = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    // WRAP にすることで、ノイズ空間が無限に繰り返されるようになります
    sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
    sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
    sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    device->CreateSamplerState(&sampDesc, sampler.GetAddressOf()); // ヘッダーに追加が必要

    isLoaded = true;
}

void GPUParticleUpdateSystem::Update(float dt)
{
    if (!isLoaded) {
        InitializeParticle(Graphics::Instance().GetDevice());
        return;
    }

    ID3D11DeviceContext *dc = Graphics::Instance().GetDeviceContext();

    // リソース競合を防ぐため、CS実行前にSRVスロットを掃除する
    // (特にパーティクルバッファがPS/VSに残っているとInitCSが失敗するため)
    ID3D11ShaderResourceView *nullSRVs[16] = {nullptr};
    dc->VSSetShaderResources(0, 16, nullSRVs);
    dc->PSSetShaderResources(0, 16, nullSRVs);
    dc->CSSetShaderResources(0, 16, nullSRVs); // CS自身の入力も一旦クリア
    dc->GSSetShaderResources(0, 16, nullSRVs);

    // 各エンティティのパーティクルを更新
    ForEachWithID(
        [&](CCL::ECS::EntityID       id,
            GPUParticleComponent    &particle,
            const  TransformComponent &transform) {
            // -----------------------------------------------------------
            // プールコンポーネントを手動で取得してチェック
            // -----------------------------------------------------------

            // 1. 自身のスリープ判定 (既存の処理)
            if (auto *pooled = _world->GetComponent<PooledParticleComponent>(id)) {
                if (pooled->isSleeping) return;
            }

            // =========================================================
            // 親が「待機中（地下にいる）の弾丸」なら処理をスキップ
            // =========================================================
            if (transform.parentID != 0) {
                if (auto *parentPool =
                        _world->GetComponent<PooledBulletComponent>(transform.parentID)) {
                    if (!parentPool->isActive) {
                        return; // 親が寝ているなら何もしない（CSのDispatchも呼ばない）
                    }
                }
            }

            // 例: 親がいない(プール待機中)なら更新しない
            //if (transform.parentID == 0) return;

            // 1. リソース未作成なら作成 (これはそのまま)
            if (!particle.isInitialized) {
                RebuildParticleBuffer(particle);
            }

            // ハンドルからバッファ一式を取得
            GpuParticleBufferSet* buffers = ResourceManager::Instance().GetParticleBuffer(particle.computeBuffer);
            if (!buffers) return; // バッファがなければ何もできない

            // 2. 初期化シェーダーの実行判定
            // 「リソースがある」かつ「GPU初期化が必要」なら実行する
            if (particle.isInitialized && particle.needsGpuInit && initCS) {
                DispatchInitCS(dc, particle, transform, buffers);
            }

            // 4. 通常の更新として UpdateCS を走らせる
            if (particle.isInitialized && !particle.needsGpuInit && updateCS) {
                DispatchUpdateCS(dc, particle, transform, dt, buffers);
            }

    });
}

// =================================================================================
//  分割されたヘルパー関数群
// =================================================================================

void GPUParticleUpdateSystem::RebuildParticleBuffer(GPUParticleComponent &particle)
{
    // 古いバッファの破棄
    if (particle.computeBuffer.IsValid()) {
        ResourceManager::Instance().UnloadParticleBuffer(particle.computeBuffer);
        particle.computeBuffer = ParticleBufferHandle{};
    }

    ID3D11Device *device = Graphics::Instance().GetDevice();
    if (!device || particle.config.max_particle_count <= 0) return;

    // 新しいバッファの生成
    particle.computeBuffer = ResourceManager::Instance().CreateParticleBuffer();
    GpuParticleBufferSet *buffers =
        ResourceManager::Instance().GetParticleBuffer(particle.computeBuffer);

    if (buffers) {
        D3D11_BUFFER_DESC sbDesc   = {};
        sbDesc.ByteWidth           = sizeof(Particle) * particle.config.max_particle_count;
        sbDesc.Usage               = D3D11_USAGE_DEFAULT;
        sbDesc.BindFlags           = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
        sbDesc.MiscFlags           = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        sbDesc.StructureByteStride = sizeof(Particle);
        device->CreateBuffer(&sbDesc, nullptr, &buffers->particleBuffer);

        D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.Format                           = DXGI_FORMAT_UNKNOWN;
        uavDesc.ViewDimension                    = D3D11_UAV_DIMENSION_BUFFER;
        uavDesc.Buffer.NumElements               = particle.config.max_particle_count;
        device->CreateUnorderedAccessView(buffers->particleBuffer, &uavDesc, &buffers->particleUAV);

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format                          = DXGI_FORMAT_UNKNOWN;
        srvDesc.ViewDimension                   = D3D11_SRV_DIMENSION_BUFFER;
        srvDesc.Buffer.NumElements              = particle.config.max_particle_count;
        device->CreateShaderResourceView(buffers->particleBuffer, &srvDesc, &buffers->particleSRV);

        D3D11_BUFFER_DESC cbDesc = {};
        cbDesc.ByteWidth         = sizeof(ParticleSystemConfig);
        if (cbDesc.ByteWidth % 16 != 0) cbDesc.ByteWidth = ((cbDesc.ByteWidth + 15) / 16) * 16;
        cbDesc.Usage     = D3D11_USAGE_DEFAULT;
        cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        device->CreateBuffer(&cbDesc, nullptr, &buffers->constantBuffer);

        if (particle.texturePath[0] != '\0') {
            particle.texture = ResourceManager::Instance().LoadTexture(particle.texturePath);
        }
        if (particle.colorRampPath[0] != '\0') {
            particle.colorRamp = ResourceManager::Instance().LoadTexture(particle.colorRampPath);
        }

        particle.isInitialized = true;
        particle.needsGpuInit  = true;
    }
}

void GPUParticleUpdateSystem::DispatchInitCS(ID3D11DeviceContext *dc,
    GPUParticleComponent                                         &particle,
    const TransformComponent                                     &transform,
    GpuParticleBufferSet                                         *buffers)
{
    particle.config.world_position    = transform.GetWorldPosition();
    particle.config.emission_rotation = transform.rotation;
    if (particle.config.delta_time == 0.0f) particle.config.delta_time = 0.016f;

    if (buffers->constantBuffer) {
        dc->UpdateSubresource(buffers->constantBuffer, 0, nullptr, &particle.config, 0, 0);
        ID3D11Buffer *cbs[] = {buffers->constantBuffer};
        dc->CSSetConstantBuffers(ShaderResources::CbParticle, 1, cbs);
    }

    dc->CSSetShader(initCS.Get(), nullptr, 0);
    ID3D11UnorderedAccessView *uavs[] = {buffers->particleUAV};
    dc->CSSetUnorderedAccessViews(ShaderResources::UavParticleBuf, 1, uavs, nullptr);

    UINT groups = (particle.config.max_particle_count + 15) / 16;
    dc->Dispatch(groups, 1, 1);

    ID3D11UnorderedAccessView *nullUAV[] = {nullptr};
    dc->CSSetUnorderedAccessViews(ShaderResources::UavParticleBuf, 1, nullUAV, nullptr);

    particle.needsGpuInit = false;
}

void GPUParticleUpdateSystem::DispatchUpdateCS(ID3D11DeviceContext *dc,
    GPUParticleComponent                                           &particle,
    const TransformComponent                                       &transform,
    float                                                           dt,
    GpuParticleBufferSet                                           *buffers)
{
    particle.config.world_position = transform.GetWorldPosition();
    particle.config.delta_time     = dt;
    particle.config.time += dt;

    if (buffers->constantBuffer) {
        dc->UpdateSubresource(buffers->constantBuffer, 0, nullptr, &particle.config, 0, 0);
        ID3D11Buffer *cbs[] = {buffers->constantBuffer};
        dc->CSSetConstantBuffers(ShaderResources::CbParticle, 1, cbs);
    }

    dc->CSSetShader(updateCS.Get(), nullptr, 0);

    ID3D11ShaderResourceView *rampSRV = ResourceManager::Instance().GetTexture(particle.colorRamp);
    if (rampSRV) {
        ID3D11ShaderResourceView *views[] = {rampSRV};
        dc->CSSetShaderResources(ShaderResources::SrvMaterialTex1, 1, views);
    }

    if (noiseSRV) {
        ID3D11ShaderResourceView *views[] = {noiseSRV.Get()};
        dc->CSSetShaderResources(ShaderResources::SrvMaterialTex2, 1, views);
    }

    if (sampler) {
        ID3D11SamplerState *samplers[] = {sampler.Get()};
        dc->CSSetSamplers(ShaderResources::SmpLinear, 1, samplers);
    }

    if (buffers->particleUAV) {
        ID3D11UnorderedAccessView *uavs[] = {buffers->particleUAV};
        dc->CSSetUnorderedAccessViews(ShaderResources::UavParticleBuf, 1, uavs, nullptr);
    }

    UINT groups = (particle.config.max_particle_count + 15) / 16;
    dc->Dispatch(groups, 1, 1);

    ID3D11UnorderedAccessView *nullUAV[] = {nullptr};
    dc->CSSetUnorderedAccessViews(ShaderResources::UavParticleBuf, 1, nullUAV, nullptr);

    ID3D11ShaderResourceView *nullSRV[] = {nullptr};
    dc->CSSetShaderResources(ShaderResources::SrvMaterialTex1, 1, nullSRV);

    if (particle.config.emission_mode == 1) {
        particle.config.burst_trigger = 0.0f;
    }
}


// ★追加: ノイズテクスチャ生成関数
void GPUParticleUpdateSystem::CreateNoiseTexture(ID3D11Device *device)
{
    const int          size = 32;                         // 32x32x32 のノイズボリューム
    std::vector<float> noiseData(size * size * size * 4); // RGBA float

    // 乱数生成器
    std::mt19937                          gen(12345);
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);

    for (int i = 0; i < size * size * size; ++i) {
        // ベクトル場として使うため、xyzにランダムな方向を入れる
        // 0.0~1.0 の値を入れ、シェーダー側で -1.0~1.0 に展開して使う
        noiseData[i * 4 + 0] = dist(gen); // x
        noiseData[i * 4 + 1] = dist(gen); // y
        noiseData[i * 4 + 2] = dist(gen); // z
        noiseData[i * 4 + 3] = 0.0f;      // w (未使用)
    }

    // Texture3D 作成
    D3D11_TEXTURE3D_DESC desc = {};
    desc.Width                = size;
    desc.Height               = size;
    desc.Depth                = size;
    desc.MipLevels            = 1;
    desc.Format               = DXGI_FORMAT_R32G32B32A32_FLOAT;
    desc.Usage                = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags            = D3D11_BIND_SHADER_RESOURCE;
    desc.CPUAccessFlags       = 0;

    D3D11_SUBRESOURCE_DATA initData = {};
    initData.pSysMem                = noiseData.data();
    initData.SysMemPitch            = size * sizeof(float) * 4;
    initData.SysMemSlicePitch       = size * size * sizeof(float) * 4;

    device->CreateTexture3D(&desc, &initData, noiseTexture.GetAddressOf());

    // SRV 作成
    device->CreateShaderResourceView(noiseTexture.Get(), nullptr, noiseSRV.GetAddressOf());
}


REGISTER_RENDER_SYSTEM(GPUParticleUpdateSystem, Priority::RenderStage::PreMain);