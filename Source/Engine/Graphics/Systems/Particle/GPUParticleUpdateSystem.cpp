#include "GPUParticleUpdateSystem.h"
#include "Engine/Graphics/RHI/Graphics.h"
#include "Engine/Graphics/RHI/DebugName.h"
#include "Engine/Graphics/Resource/ResourceManager.h"
#include "Engine/Graphics/Shader/ShaderResources.h"
#include "Engine/Graphics/Shader/Shared/ParticleShared.h"
#include "Engine/Graphics/Shader/Material/ParticleComputeShader.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include <random>


#include "Engine/Graphics/Shader/ShaderReflection.h"

#include <tracy/Tracy.hpp>

GPUParticleUpdateSystem::~GPUParticleUpdateSystem() = default;

// =========================================================================
// BuildStaticCb — desc (権威データ) から CbParticleStatic を組み立てる
//   desc が変更された時のみ呼ばれる (ParticleEmitter::staticDirty フラグ管理)。
// =========================================================================
static GpuData::CbParticleStatic BuildStaticCb(const ParticleEmitterDesc& d)
{
    GpuData::CbParticleStatic cb = {};
    cb.emit_local_offset = { d.localOffset.x, d.localOffset.y, d.localOffset.z, d.localOffsetY };
    cb.emit_radius = d.emitRadius;
    cb.emit_size = d.emitSize;
    cb.size_random = d.sizeRandom;
    cb.cone_angle = d.coneAngle;
    cb.emit_speed = d.emitSpeed;
    cb.emit_angular_speed = d.angularSpeed;
    cb.lifespan = d.lifespan;
    cb.spawn_delay = d.spawnDelay;
    cb.fade_duration = d.fadeDuration;
    cb.manual_color = d.color;
    cb.noise_scale = d.noiseScale;
    cb.noise_strength = d.noiseStrength;
    cb.gravity = d.gravity;
    cb.max_speed = d.maxSpeed;
    cb.velocity_stretch = d.velocityStretch;
    cb.uv_scroll_speed = d.uvScrollSpeed;
    cb.emit_stretch = d.emitStretch;
    cb.particle_scale = d.scale;
    cb.sprite_sheet_grid = d.spriteGrid;
    cb.sprite_anim_mode = d.spriteMode;
    cb.render_mode = d.renderMode;
    cb.color_mode = d.colorMode;
    cb.particle_type = d.particleType;
    cb.max_count = d.maxCount;
    cb.emit_mode = d.emitMode;
    cb.burst_count = d.burstCount;

    // ---- Force Field ----
    cb.attractor_pos = { d.attractorPos.x, d.attractorPos.y, d.attractorPos.z, d.attractorStrength };
    cb.attractor_radius = d.attractorRadius;
    cb.vortex_strength = d.vortexStrength;
    cb.drag = d.drag;
    uint32_t flags = 0u;
    if (d.attractorStrength != 0.0f || d.vortexStrength != 0.0f) flags |= 1u; // 引力/渦 有効
    if (d.attractorLocal)                                        flags |= 2u; // local 空間
    cb.force_flags = flags;

    // ---- 発生形状 (線分/剣) ----
    cb.emit_line_end = { d.lineEnd.x, d.lineEnd.y, d.lineEnd.z };
    cb.emit_shape    = static_cast<float>(d.emitShape);

    return cb;
}

// =========================================================================
// BuildDynamicCb — ランタイム値から CbParticleDynamic を組み立てる
//   毎フレーム呼ばれる (48 bytes)。
// =========================================================================
static GpuData::CbParticleDynamic BuildDynamicCb(
    const TransformComponent& transform,
    const GpuData::float3& prevPos, const GpuData::float4& prevRot,
    float time, float dt, float burstTrigger, float emitEnabled,
    GpuData::uint instanceSeed)
{
    GpuData::CbParticleDynamic cb = {};
    // world 回転を使う（local ではない）。ボーン追従(BoneAttachmentSystem)は
    //   worldMatrix にボーンの向きを焼き込むが local rotation は更新しないため、
    //   transform.rotation だと剣の向きに追従せず線分/速度の向きがずれる。
    cb.emit_rotation = transform.GetWorldRotation();
    cb.world_position = transform.GetWorldPosition();
    cb.time = time;
    cb.delta_time = dt;
    cb.burst_trigger = burstTrigger;
    cb.emit_enabled = emitEnabled; // 継続放出の発生ゲート(アニメ連動)。false時は再生成を止める。
    // エミッタ実体ごとの乱数種。spawn() がこれを混ぜて粒子ごとの種を作るので、
    //   同じプレハブから出した VFX でも粒子配置が個体ごとに変わる。
    cb.instance_seed = instanceSeed;
    // 前フレーム姿勢: spawn 側で現フレームとの間を補間してスポーンを軌道上に連続配置する。
    cb.prev_emit_rotation  = prevRot;
    cb.prev_world_position = prevPos;
    return cb;
}

// =========================================================================
void GPUParticleUpdateSystem::Initialize()
{
    ZoneScopedN("GPUParticle: Initialize");

    ID3D11Device* device = Graphics::Instance().GetDevice();
    if (!device) return;

    m_device = device;
    {
        ZoneScopedN("GPUParticle: ComputeShader compile");
        m_computeShader = std::make_unique<ParticleComputeShader>(device);
    }

    D3D11_SAMPLER_DESC sd = {};
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    device->CreateSamplerState(&sd, m_sampler.GetAddressOf());

    {
        ZoneScopedN("GPUParticle: CreateNoiseTexture (32^3)");
        CreateNoiseTexture(device);
    }
}

void GPUParticleUpdateSystem::Update(float dt)
{
    ZoneScopedN("GPUParticleUpdateSystem::Update");

    ID3D11DeviceContext* dc = Graphics::Instance().GetDeviceContext();
    if (!dc || !m_computeShader) return;

    {
        ZoneScopedN("GPUParticle: CheckAndReload (shader)");
        m_computeShader->CheckAndReload(m_device);
    }
    if (!m_computeShader->GetInitCS() || !m_computeShader->GetUpdateCS()) return;

    // CS の UAV バインド前に前フレームの SRV スロットを全解放
    ID3D11ShaderResourceView* nullSRVs[16] = {};
    dc->VSSetShaderResources(0, 16, nullSRVs);
    dc->GSSetShaderResources(0, 16, nullSRVs);
    dc->PSSetShaderResources(0, 16, nullSRVs);
    dc->CSSetShaderResources(0, 16, nullSRVs);

    // 世界の時計が止まっている間はシミュレーションを凍らせる。
    //   本システムは RenderStage なのでポーズ中(UpdatePaused)も走り続けるが、localDt を作る
    //   TimeScaleSystem は LOGIC なので止まっており localDt が古い値のまま残る。ここで
    //   TimeContext.isPaused を見て dt=0 にすれば particle.time も emission も進まず完全に静止する。
    bool worldPaused = false;
    if (m_world->HasResource<TimeContext>())
        worldPaused = m_world->GetResource<TimeContext>().isPaused;

    ZoneNamedN(dispatchZone, "GPUParticle: Emitter dispatch loop", true);
    ForEach([&](GPUParticleComponent& particle,
        const TransformComponent& transform,
        const TimeStateComponent& rawDt)
        {
            // ---- 効果全体で共通の値を、エミッタのループに入る前に1回だけ確定させる ----
            //   特に time は「エンティティの時間」なので、ループ内で進めると
            //   エミッタ数ぶん多重に加算されてしまう。
            EffectFrame frame;
            frame.dt = worldPaused ? 0.0f : rawDt.localDt; // ポーズ中は dt=0 で凍結
            particle.time += frame.dt;
            frame.time = particle.time;

            const GpuData::float3 curPos = transform.GetWorldPosition();
            const GpuData::float4 curRot = transform.GetWorldRotation();
            frame.prevPos = particle.hasPrevTransform ? particle.prevWorldPos : curPos;
            frame.prevRot = particle.hasPrevTransform ? particle.prevWorldRot : curRot;

            // ---- エミッタごとに Dispatch ----
            for (ParticleEmitter& emitter : particle.emitters)
            {
                // 実体ごとの乱数種を一度だけ採番する（0 = 未割り当て）。
                //   ここで採るのは、プレハブ生成・シーン読込・エディタでの追加の
                //   どの経路で作られても必ず通る唯一の場所だから。
                if (emitter.instanceSeed == 0)
                    emitter.instanceSeed = NextInstanceSeed();

                if (!emitter.isInitialized)
                    RebuildParticleBuffer(emitter);

                GPUParticleBufferSet* buffers =
                    ResourceManager::Instance().GetParticleBuffer(emitter.computeBuffer);
                if (!buffers || !buffers->particleUAV) continue;

                if (emitter.needsGpuInit)
                    DispatchInitCS(dc, m_computeShader.get(), emitter, transform, buffers, frame);
                else
                    DispatchUpdateCS(dc, m_computeShader.get(), emitter, transform, buffers, frame);
            }

            // 次フレームのために現在姿勢を保存（全エミッタで共通なのでループの外）。
            particle.prevWorldPos     = curPos;
            particle.prevWorldRot     = curRot;
            particle.hasPrevTransform = true;
        });

    dc->CSSetShader(nullptr, nullptr, 0);
}

// =========================================================================
// [WARNING] フレームスパイク注意: CreateBuffer を呼ぶ重い処理。
//   将来 ResourceManager でバッファプール化を推奨。
// =========================================================================
GpuData::uint GPUParticleUpdateSystem::NextInstanceSeed()
{
    const GpuData::uint seed = m_nextInstanceSeed++;
    if (m_nextInstanceSeed == 0) m_nextInstanceSeed = 1; // 一周したら 0 を飛ばす
    return seed;
}

// =========================================================================
void GPUParticleUpdateSystem::RebuildParticleBuffer(ParticleEmitter& emitter)
{
    ZoneScopedN("GPUParticle: RebuildParticleBuffer (CreateBuffer)");

    if (emitter.computeBuffer.IsValid()) {
        ResourceManager::Instance().UnloadParticleBuffer(emitter.computeBuffer);
        emitter.computeBuffer = ParticleBufferHandle{};
    }

    ID3D11Device* device = Graphics::Instance().GetDevice();
    if (!device || emitter.desc.maxCount <= 0) return;

    // 重いバッファ生成処理は全て ResourceManager に任せる（ゾンビコードの削除）
    emitter.computeBuffer = ResourceManager::Instance().CreateParticleBuffer(device, emitter.desc.maxCount);

    emitter.isInitialized = true;
    emitter.needsGpuInit  = true;
    emitter.staticDirty   = true;
    // ※ time は効果全体の値なので、1エミッタの作り直しではリセットしない。
}

// =========================================================================
// InitCS を1エミッタ分 Dispatch する。
// =========================================================================
void GPUParticleUpdateSystem::DispatchInitCS(
    ID3D11DeviceContext* dc,
    ParticleComputeShader* cs,
    ParticleEmitter& emitter,
    const TransformComponent& transform,
    GPUParticleBufferSet* buf,
    const EffectFrame& frame)
{
    ZoneScopedN("GPUParticle: DispatchInitCS (first frame)");

    if (!cs || !buf) return;

    // Static CB: 初回は必ず upload (.Get() を追加)
    auto staticCb = BuildStaticCb(emitter.desc);
    dc->UpdateSubresource(buf->staticCB.Get(), 0, nullptr, &staticCb, 0, 0);
    emitter.staticDirty = false;

    // Dynamic CB (InitCS では dt は 0.016f で代替) (.Get() を追加)
    //   emit ゲートを init にも渡す: OFF なら継続モードの pre-warm を抑止し、空から開始する。
    //   init 時は前フレーム姿勢が無いので現在姿勢で初期化(スメアなし)。
    const GpuData::float3 curPos = transform.GetWorldPosition();
    const GpuData::float4 curRot = transform.GetWorldRotation();
    auto dynCb = BuildDynamicCb(transform, curPos, curRot, frame.time, 0.016f, emitter.burstTrigger,
        emitter.emitting ? 1.0f : 0.0f, emitter.instanceSeed);
    dc->UpdateSubresource(buf->dynamicCB.Get(), 0, nullptr, &dynCb, 0, 0);

    // b13, b14 を一括バインド (.Get() を追加)
    ID3D11Buffer* cbs[2] = { buf->staticCB.Get(), buf->dynamicCB.Get() };
    dc->CSSetConstantBuffers(SLOT_CB_PARTICLE_STATIC, 2, cbs);

    dc->CSSetShader(cs->GetInitCS(), nullptr, 0);

    // UAV バインド (.Get() を追加)
    ID3D11UnorderedAccessView* uavs[] = { buf->particleUAV.Get() };
    dc->CSSetUnorderedAccessViews(SLOT_UAV_PARTICLE_BUF, 1, uavs, nullptr);

    UINT groups = (static_cast<UINT>(emitter.desc.maxCount) + 15) / 16;
    dc->Dispatch(groups, 1, 1);

    ID3D11UnorderedAccessView* nullUAV[] = { nullptr };
    dc->CSSetUnorderedAccessViews(SLOT_UAV_PARTICLE_BUF, 1, nullUAV, nullptr);

    emitter.needsGpuInit = false;
}

// =========================================================================
// UpdateCS を1エミッタ分 Dispatch する。
// =========================================================================
void GPUParticleUpdateSystem::DispatchUpdateCS(
    ID3D11DeviceContext* dc,
    ParticleComputeShader* cs,
    ParticleEmitter& emitter,
    const TransformComponent& transform,
    GPUParticleBufferSet* buf,
    const EffectFrame& frame)
{
    ZoneScopedN("GPUParticle: DispatchUpdateCS");

    if (!cs || !buf) return;

    // Static CB: desc が変更された時のみ upload (.Get() を追加)
    if (emitter.staticDirty) {
        auto staticCb = BuildStaticCb(emitter.desc);
        dc->UpdateSubresource(buf->staticCB.Get(), 0, nullptr, &staticCb, 0, 0);
        emitter.staticDirty = false;
    }

    // Dynamic CB: 毎フレーム upload (.Get() を追加)
    //   前フレーム姿勢を渡してサブフレーム補間（スポーンを軌道上に連続配置）。
    //   time / dt / 前フレーム姿勢は効果全体で共通なので frame から受け取る。
    auto dynCb = BuildDynamicCb(transform, frame.prevPos, frame.prevRot,
        frame.time, frame.dt, emitter.burstTrigger,
        emitter.emitting ? 1.0f : 0.0f, emitter.instanceSeed);

    dc->UpdateSubresource(buf->dynamicCB.Get(), 0, nullptr, &dynCb, 0, 0);

    // b13, b14 を一括バインド (.Get() を追加)
    ID3D11Buffer* cbs[2] = { buf->staticCB.Get(), buf->dynamicCB.Get() };
    dc->CSSetConstantBuffers(SLOT_CB_PARTICLE_STATIC, 2, cbs);

    dc->CSSetShader(cs->GetUpdateCS(), nullptr, 0);

    // カラーランプ (t1)
    if (auto* ramp = ResourceManager::Instance().GetTexture(emitter.colorRamp)) {
        ID3D11ShaderResourceView* v[] = { ramp };
        dc->CSSetShaderResources(SLOT_SRV_MAT_TEX1, 1, v);
    }
    // ノイズ (t2)
    if (m_noiseSRV) {
        ID3D11ShaderResourceView* v[] = { m_noiseSRV.Get() };
        dc->CSSetShaderResources(SLOT_SRV_MAT_TEX2, 1, v);
    }
    // サンプラー (s1)
    if (m_sampler) {
        ID3D11SamplerState* s[] = { m_sampler.Get() };
        dc->CSSetSamplers(SLOT_SMP_LINEAR, 1, s);
    }

    // UAV バインド (.Get() を追加)
    ID3D11UnorderedAccessView* uavs[] = { buf->particleUAV.Get() };
    dc->CSSetUnorderedAccessViews(SLOT_UAV_PARTICLE_BUF, 1, uavs, nullptr);

    UINT groups = (static_cast<UINT>(emitter.desc.maxCount) + 15) / 16;
    dc->Dispatch(groups, 1, 1);

    // 後始末
    ID3D11UnorderedAccessView* nullUAV[] = { nullptr };
    dc->CSSetUnorderedAccessViews(SLOT_UAV_PARTICLE_BUF, 1, nullUAV, nullptr);
    ID3D11ShaderResourceView* nullSRV[] = { nullptr };
    dc->CSSetShaderResources(SLOT_SRV_MAT_TEX1, 1, nullSRV);
    dc->CSSetShaderResources(SLOT_SRV_MAT_TEX2, 1, nullSRV);

    // 発火要求は1フレームで使い切る。
    //   継続モードでも必ず消すこと。継続中はシェーダが burst_trigger を読まないので、
    //   立てっぱなしのまま Burst へ切り替えると、その瞬間に意図しない発火が起きる。
    emitter.burstTrigger = 0.0f;
}

void GPUParticleUpdateSystem::CreateNoiseTexture(ID3D11Device* device)
{
    const int size = 32;
    std::vector<float> data(static_cast<size_t>(size) * size * size * 4);
    std::mt19937 gen(12345);
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);
    for (int i = 0; i < size * size * size; ++i) {
        data[i * 4 + 0] = dist(gen);
        data[i * 4 + 1] = dist(gen);
        data[i * 4 + 2] = dist(gen);
        data[i * 4 + 3] = 0.0f;
    }
    D3D11_TEXTURE3D_DESC desc = {};
    desc.Width = desc.Height = desc.Depth = size;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA init = {};
    init.pSysMem = data.data();
    init.SysMemPitch = size * sizeof(float) * 4;
    init.SysMemSlicePitch = size * size * sizeof(float) * 4;
    device->CreateTexture3D(&desc, &init, m_noiseTexture.GetAddressOf());
    device->CreateShaderResourceView(m_noiseTexture.Get(), nullptr, m_noiseSRV.GetAddressOf());

    SetDebugName(m_noiseSRV.Get(), "GPUParticleUpdateSystem_NoiseSRV");
}

REGISTER_RENDER_SYSTEM(GPUParticleUpdateSystem, Priority::RenderStage::R07_PreMain);
