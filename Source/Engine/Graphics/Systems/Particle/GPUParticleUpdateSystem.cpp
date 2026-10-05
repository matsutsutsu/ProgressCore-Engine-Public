#include "GPUParticleUpdateSystem.h"
#include "Engine/Graphics/RHI/Graphics.h"
#include "Engine/Graphics/RHI/DebugName.h"
#include "Engine/Graphics/Resource/ResourceManager.h"
#include "Engine/Graphics/Shader/ShaderResources.h"
#include "Engine/Graphics/Shader/Shared/ParticleShared.h"
#include "Engine/Graphics/Shader/Material/ParticleComputeShader.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include <algorithm>
#include <cmath>
#include <random>

#include "Engine/Graphics/Shader/ShaderReflection.h"

#include <tracy/Tracy.hpp>

GPUParticleUpdateSystem::~GPUParticleUpdateSystem() = default;

// =========================================================================
// BuildStaticCb — desc（エミッタの設定）から CbParticleStatic を組み立てる
//   desc が変わったとき（ParticleEmitter::staticDirty）と、初期化のときだけ呼ぶ。
// =========================================================================
// 画面で見た色（sRGB）を、シェーダーが計算に使うリニアな値へ直す。
//   ToneMapPS が最後に pow(x, 1/2.2) で画面用に戻すので、ここで 2.2 乗しておくと
//   インスペクタで選んだ色がそのまま画面に出る。直さないと 0.3 が 0.58 まで持ち上がり、
//   濃いオレンジが薄いオレンジになる（粒子の色が全体に薄く見えていた原因の1つ）。
//   アルファは色ではなく割合なので変えない。
static GpuData::float4 SrgbToLinear(const GpuData::float4 &c)
{
    const auto toLin = [](float v) { return std::pow((std::max)(v, 0.0f), 2.2f); };
    return {toLin(c.x), toLin(c.y), toLin(c.z), c.w};
}

static GpuData::CbParticleStatic BuildStaticCb(const ParticleEmitterDesc &d)
{
    GpuData::CbParticleStatic cb = {};
    cb.emit_local_offset         = {d.localOffset.x, d.localOffset.y, d.localOffset.z, d.localOffsetY};
    cb.emit_radius               = d.emitRadius;
    cb.emit_size                 = d.emitSize;
    cb.size_random               = d.sizeRandom;
    cb.cone_angle                = d.coneAngle;
    cb.emit_speed                = d.emitSpeed;
    cb.emit_angular_speed        = d.angularSpeed;
    cb.lifespan                  = d.lifespan;
    cb.spawn_delay               = d.spawnDelay;
    cb.fade_duration             = d.fadeDuration;
    cb.manual_color              = SrgbToLinear(d.color);
    cb.intensity                 = d.intensity;
    cb.noise_scale               = d.noiseScale;
    cb.noise_strength            = d.noiseStrength;
    cb.gravity                   = d.gravity;
    cb.max_speed                 = d.maxSpeed;
    cb.velocity_stretch          = d.velocityStretch;
    cb.uv_scroll_speed           = d.uvScrollSpeed;
    cb.emit_stretch              = d.emitStretch;
    cb.particle_scale            = d.scale;
    cb.sprite_sheet_grid         = d.spriteGrid;
    cb.sprite_anim_mode          = d.spriteMode;
    cb.orient_mode               = d.orientMode;
    cb.render_mode               = d.renderMode;
    cb.color_mode                = d.colorMode;
    cb.particle_type             = d.particleType;
    cb.max_count                 = d.maxCount;
    cb.emit_mode                 = d.emitMode;
    cb.burst_count               = d.burstCount;

    // ---- Force Field ----
    cb.attractor_pos    = {d.attractorPos.x, d.attractorPos.y, d.attractorPos.z, d.attractorStrength};
    cb.attractor_radius = d.attractorRadius;
    cb.vortex_strength  = d.vortexStrength;
    cb.drag             = d.drag;
    uint32_t flags      = 0u;
    if (d.attractorStrength != 0.0f || d.vortexStrength != 0.0f) flags |= 1u; // 引力/渦 有効
    if (d.attractorLocal) flags |= 2u;                                        // local 空間
    cb.force_flags = flags;

    // ---- 発生形状 (線分/剣) ----
    cb.emit_line_end = {d.lineEnd.x, d.lineEnd.y, d.lineEnd.z};
    cb.emit_shape    = static_cast<float>(d.emitShape);

    return cb;
}

// =========================================================================
// BuildDynamicCb — 実行時の値から CbParticleDynamic（80 バイト）を組み立てる。毎フレーム呼ぶ
// =========================================================================
static GpuData::CbParticleDynamic BuildDynamicCb(
    const TransformComponent &transform,
    const GpuData::float3 &prevPos, const GpuData::float4 &prevRot,
    float time, float dt, float burstTrigger, float emitEnabled,
    GpuData::uint instanceSeed)
{
    GpuData::CbParticleDynamic cb = {};
    // ローカルではなくワールドの回転を使う。ボーン追従（BoneAttachmentSystem）は
    //   ワールド行列にボーンの向きを入れるが、ローカルの rotation は更新しないので、
    //   transform.rotation だと剣の向きに付いていかず、線分や速度の向きがずれる。
    cb.emit_rotation  = transform.GetWorldRotation();
    cb.world_position = transform.GetWorldPosition();
    cb.time           = time;
    cb.delta_time     = dt;
    cb.burst_trigger  = burstTrigger;
    cb.emit_enabled   = emitEnabled; // 継続放出のオン/オフ。0 なら新しい粒子を出さない
    // エミッタごとの乱数の種。spawn() がこれを混ぜて粒子ごとの種を作るので、
    //   同じプレハブから出した VFX でも、個体ごとに粒子の配置が変わる。
    cb.instance_seed = instanceSeed;
    // 前フレームの姿勢。spawn 側で今のフレームとの間を補間し、生成位置を軌道上に並べる
    cb.prev_emit_rotation  = prevRot;
    cb.prev_world_position = prevPos;
    return cb;
}

// =========================================================================
void GPUParticleUpdateSystem::Initialize()
{
    ZoneScopedN("GPUParticle: Initialize");

    ID3D11Device *device = Graphics::Instance().GetDevice();
    if (!device) return;

    m_device = device;
    {
        ZoneScopedN("GPUParticle: ComputeShader compile");
        m_computeShader = std::make_unique<ParticleComputeShader>(device);
    }

    D3D11_SAMPLER_DESC sd = {};
    sd.Filter             = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
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

    ID3D11DeviceContext *dc = Graphics::Instance().GetDeviceContext();
    if (!dc || !m_computeShader) return;

    {
        ZoneScopedN("GPUParticle: CheckAndReload (shader)");
        m_computeShader->CheckAndReload(m_device);
    }
    if (!m_computeShader->GetInitCS() || !m_computeShader->GetUpdateCS()) return;

    // UAV をバインドする前に、同じバッファを SRV として持っているスロットをすべて外す
    ID3D11ShaderResourceView *nullSRVs[16] = {};
    dc->VSSetShaderResources(0, 16, nullSRVs);
    dc->GSSetShaderResources(0, 16, nullSRVs);
    dc->PSSetShaderResources(0, 16, nullSRVs);
    dc->CSSetShaderResources(0, 16, nullSRVs);

    // 世界の時計が止まっている間は、シミュレーションを止める。
    //   この System は描画ステージなのでポーズ中（UpdatePaused）も走るが、localDt を作る
    //   TimeScaleSystem はロジックなので止まっていて、localDt は古い値のまま残る。
    //   TimeContext.isPaused を見て dt=0 にすれば、time も発生も進まず完全に止まる。
    bool worldPaused = false;
    if (m_world->HasResource<TimeContext>())
        worldPaused = m_world->GetResource<TimeContext>().isPaused;

    ZoneNamedN(dispatchZone, "GPUParticle: Emitter dispatch loop", true);
    ForEach([&](GPUParticleComponent     &particle,
                const TransformComponent &transform,
                const TimeStateComponent &rawDt) {
        // ---- エフェクト全体で共通の値を、エミッタのループの前に1回だけ決める ----
        //   time は Entity の時間なので、ループ内で進めるとエミッタの数だけ重ねて足される。
        EffectFrame frame;
        frame.dt = worldPaused ? 0.0f : rawDt.localDt;
        particle.time += frame.dt;
        frame.time = particle.time;

        const GpuData::float3 curPos = transform.GetWorldPosition();
        const GpuData::float4 curRot = transform.GetWorldRotation();
        frame.prevPos                = particle.hasPrevTransform ? particle.prevWorldPos : curPos;
        frame.prevRot                = particle.hasPrevTransform ? particle.prevWorldRot : curRot;

        // ---- エミッタごとに Dispatch ----
        for (ParticleEmitter &emitter : particle.emitters)
        {
            // エミッタごとの乱数の種を一度だけ決める（0 は未割り当て）。
            //   ここで決めるのは、プレハブ生成・シーン読み込み・エディタでの追加の
            //   どの経路で作られても必ず通る場所がここだけだから。
            if (emitter.instanceSeed == 0)
                emitter.instanceSeed = NextInstanceSeed();

            if (!emitter.isInitialized)
                RebuildParticleBuffer(emitter);

            GPUParticleBufferSet *buffers =
                ResourceManager::Instance().GetParticleBuffer(emitter.computeBuffer);
            if (!buffers || !buffers->particleUAV) continue;

            if (emitter.needsGpuInit)
                DispatchInitCS(dc, m_computeShader.get(), emitter, transform, buffers, frame);
            else
                DispatchUpdateCS(dc, m_computeShader.get(), emitter, transform, buffers, frame);
        }

        // 次のフレームのために今の姿勢を残す（全エミッタで共通なのでループの外）
        particle.prevWorldPos     = curPos;
        particle.prevWorldRot     = curRot;
        particle.hasPrevTransform = true;
    });

    dc->CSSetShader(nullptr, nullptr, 0);
}

// =========================================================================
GpuData::uint GPUParticleUpdateSystem::NextInstanceSeed()
{
    const GpuData::uint seed = m_nextInstanceSeed++;
    if (m_nextInstanceSeed == 0) m_nextInstanceSeed = 1; // 一周したら 0 を飛ばす
    return seed;
}

// =========================================================================
void GPUParticleUpdateSystem::RebuildParticleBuffer(ParticleEmitter &emitter)
{
    ZoneScopedN("GPUParticle: RebuildParticleBuffer (CreateBuffer)");

    if (emitter.computeBuffer.IsValid())
    {
        ResourceManager::Instance().UnloadParticleBuffer(emitter.computeBuffer);
        emitter.computeBuffer = ParticleBufferHandle{};
    }

    ID3D11Device *device = Graphics::Instance().GetDevice();
    if (!device || emitter.desc.maxCount <= 0) return;

    // TODO(2026-03-27): ResourceManager で粒子バッファをプールし、作り直しの処理時間の跳ねを無くす
    emitter.computeBuffer = ResourceManager::Instance().CreateParticleBuffer(device, emitter.desc.maxCount);

    emitter.isInitialized = true;
    emitter.needsGpuInit  = true;
    emitter.staticDirty   = true;
    // time はエフェクト全体の値なので、1エミッタを作り直しても戻さない
}

// =========================================================================
// InitCS を1エミッタ分 Dispatch する。
// =========================================================================
void GPUParticleUpdateSystem::DispatchInitCS(
    ID3D11DeviceContext      *dc,
    ParticleComputeShader    *cs,
    ParticleEmitter          &emitter,
    const TransformComponent &transform,
    GPUParticleBufferSet     *buf,
    const EffectFrame        &frame)
{
    ZoneScopedN("GPUParticle: DispatchInitCS (first frame)");

    if (!cs || !buf) return;

    // 静的 CB: 初回は必ず送る
    auto staticCb = BuildStaticCb(emitter.desc);
    dc->UpdateSubresource(buf->staticCB.Get(), 0, nullptr, &staticCb, 0, 0);
    emitter.staticDirty = false;

    // 動的 CB（InitCS では dt に 1/60 秒相当の 0.016 を使う）。
    //   発生のオン/オフも渡す。オフなら継続モードの先行生成をせず、粒子ゼロから始める。
    //   初期化時は前フレームの姿勢が無いので、今の姿勢を入れる（補間で尾を引かせない）。
    const GpuData::float3 curPos = transform.GetWorldPosition();
    const GpuData::float4 curRot = transform.GetWorldRotation();
    auto                  dynCb  = BuildDynamicCb(transform, curPos, curRot, frame.time, 0.016f, emitter.burstTrigger,
                                                  emitter.emitting ? 1.0f : 0.0f, emitter.instanceSeed);
    dc->UpdateSubresource(buf->dynamicCB.Get(), 0, nullptr, &dynCb, 0, 0);

    // b6, b7 をまとめてバインド（隣り合うスロットなので1回で済む）
    ID3D11Buffer *cbs[2] = {buf->staticCB.Get(), buf->dynamicCB.Get()};
    dc->CSSetConstantBuffers(SLOT_CB_PARTICLE_STATIC, 2, cbs);

    dc->CSSetShader(cs->GetInitCS(), nullptr, 0);

    // UAV をバインド
    ID3D11UnorderedAccessView *uavs[] = {buf->particleUAV.Get()};
    dc->CSSetUnorderedAccessViews(SLOT_UAV_PARTICLE_BUF, 1, uavs, nullptr);

    UINT groups = (static_cast<UINT>(emitter.desc.maxCount) + PARTICLE_CS_THREADS - 1) / PARTICLE_CS_THREADS;
    dc->Dispatch(groups, 1, 1);

    ID3D11UnorderedAccessView *nullUAV[] = {nullptr};
    dc->CSSetUnorderedAccessViews(SLOT_UAV_PARTICLE_BUF, 1, nullUAV, nullptr);

    emitter.needsGpuInit = false;
}

// =========================================================================
// UpdateCS を1エミッタ分 Dispatch する。
// =========================================================================
void GPUParticleUpdateSystem::DispatchUpdateCS(
    ID3D11DeviceContext      *dc,
    ParticleComputeShader    *cs,
    ParticleEmitter          &emitter,
    const TransformComponent &transform,
    GPUParticleBufferSet     *buf,
    const EffectFrame        &frame)
{
    ZoneScopedN("GPUParticle: DispatchUpdateCS");

    if (!cs || !buf) return;

    // 静的 CB: desc が変わったときだけ送る
    if (emitter.staticDirty)
    {
        auto staticCb = BuildStaticCb(emitter.desc);
        dc->UpdateSubresource(buf->staticCB.Get(), 0, nullptr, &staticCb, 0, 0);
        emitter.staticDirty = false;
    }

    // 動的 CB: 毎フレーム送る。
    //   前フレームの姿勢を渡して、生成位置を軌道上に並べる（サブフレーム補間）。
    //   time・dt・前フレームの姿勢はエフェクト全体で共通なので frame から受け取る。
    auto dynCb = BuildDynamicCb(transform, frame.prevPos, frame.prevRot,
                                frame.time, frame.dt, emitter.burstTrigger,
                                emitter.emitting ? 1.0f : 0.0f, emitter.instanceSeed);

    dc->UpdateSubresource(buf->dynamicCB.Get(), 0, nullptr, &dynCb, 0, 0);

    // b6, b7 をまとめてバインド（隣り合うスロットなので1回で済む）
    ID3D11Buffer *cbs[2] = {buf->staticCB.Get(), buf->dynamicCB.Get()};
    dc->CSSetConstantBuffers(SLOT_CB_PARTICLE_STATIC, 2, cbs);

    dc->CSSetShader(cs->GetUpdateCS(), nullptr, 0);

    // カラーランプ (t1)
    if (auto *ramp = ResourceManager::Instance().GetTexture(emitter.colorRamp))
    {
        ID3D11ShaderResourceView *v[] = {ramp};
        dc->CSSetShaderResources(SLOT_SRV_MAT_TEX1, 1, v);
    }
    // ノイズ (t2)
    if (m_noiseSRV)
    {
        ID3D11ShaderResourceView *v[] = {m_noiseSRV.Get()};
        dc->CSSetShaderResources(SLOT_SRV_MAT_TEX2, 1, v);
    }
    // サンプラー (s1)
    if (m_sampler)
    {
        ID3D11SamplerState *s[] = {m_sampler.Get()};
        dc->CSSetSamplers(SLOT_SMP_LINEAR, 1, s);
    }

    // UAV をバインド
    ID3D11UnorderedAccessView *uavs[] = {buf->particleUAV.Get()};
    dc->CSSetUnorderedAccessViews(SLOT_UAV_PARTICLE_BUF, 1, uavs, nullptr);

    UINT groups = (static_cast<UINT>(emitter.desc.maxCount) + PARTICLE_CS_THREADS - 1) / PARTICLE_CS_THREADS;
    dc->Dispatch(groups, 1, 1);

    // 次の描画で SRV として使えるよう外す
    ID3D11UnorderedAccessView *nullUAV[] = {nullptr};
    dc->CSSetUnorderedAccessViews(SLOT_UAV_PARTICLE_BUF, 1, nullUAV, nullptr);
    ID3D11ShaderResourceView *nullSRV[] = {nullptr};
    dc->CSSetShaderResources(SLOT_SRV_MAT_TEX1, 1, nullSRV);
    dc->CSSetShaderResources(SLOT_SRV_MAT_TEX2, 1, nullSRV);

    // バーストの要求は1フレームで使い切る。
    //   継続モードでも必ず消す。継続中はシェーダーが burst_trigger を読まないので、
    //   立てたままバーストに切り替えると、その瞬間に意図しないバーストが起きる。
    emitter.burstTrigger = 0.0f;
}

void GPUParticleUpdateSystem::CreateNoiseTexture(ID3D11Device *device)
{
    const int                             size = 32;
    std::vector<float>                    data(static_cast<size_t>(size) * size * size * 4);
    std::mt19937                          gen(12345);
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);
    for (int i = 0; i < size * size * size; ++i)
    {
        data[i * 4 + 0] = dist(gen);
        data[i * 4 + 1] = dist(gen);
        data[i * 4 + 2] = dist(gen);
        data[i * 4 + 3] = 0.0f;
    }
    D3D11_TEXTURE3D_DESC desc = {};
    desc.Width = desc.Height = desc.Depth = size;
    desc.MipLevels                        = 1;
    desc.Format                           = DXGI_FORMAT_R32G32B32A32_FLOAT;
    desc.Usage                            = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags                        = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA init           = {};
    init.pSysMem                          = data.data();
    init.SysMemPitch                      = size * sizeof(float) * 4;
    init.SysMemSlicePitch                 = size * size * sizeof(float) * 4;
    device->CreateTexture3D(&desc, &init, m_noiseTexture.GetAddressOf());
    device->CreateShaderResourceView(m_noiseTexture.Get(), nullptr, m_noiseSRV.GetAddressOf());

    SetDebugName(m_noiseSRV.Get(), "GPUParticleUpdateSystem_NoiseSRV");
}

REGISTER_RENDER_SYSTEM(GPUParticleUpdateSystem, Priority::RenderStage::R07_PreMain);
