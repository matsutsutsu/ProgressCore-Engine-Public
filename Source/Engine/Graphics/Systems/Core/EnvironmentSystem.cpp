#include "EnvironmentSystem.h"

#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/Graphics/Renderer/Core/ModelConstantBuffers.h"
#include "Engine/Graphics/Resource/ResourceManager.h"
#include "Engine/Graphics/Shader/ShaderResources.h"
#include "Engine/Graphics/Shader/Shared/ShaderShared.h"
#include "Engine/Graphics/RHI/GpuResourceUtils.h"

#include "Engine/Graphics/RHI/Graphics.h"
#include "Engine/Graphics/RHI/RenderContext.h" // RenderServices

#include "Engine/Graphics/Components/Lighting/FogComponent.h"
#include "Engine/Graphics/Components/Lighting/SkyboxComponent.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

EnvironmentSystem::EnvironmentSystem() : SystemBase("EnvironmentSystem")
{
}

void EnvironmentSystem::Initialize()
{
    // グローバルノイズテクスチャ
    auto noiseHandle = ResourceManager::Instance().LoadTexture("Assets/Textures/Shader/Noise.png");
    if (noiseHandle.IsValid())
        m_globalNoiseSRV = ResourceManager::Instance().GetTexture(noiseHandle);

    // 既定の IBL とスカイボックス（SkyboxComponent が無いか、パスが空のときに使う）
    auto irrHandle = ResourceManager::Instance().LoadTexture("Assets/Textures/IBL/ibl_sky_iem.dds");
    auto preHandle = ResourceManager::Instance().LoadTexture("Assets/Textures/IBL/ibl_sky_pmrem.dds");
    auto lutHandle = ResourceManager::Instance().LoadTexture("Assets/Textures/IBL/ibl_brdf_lut.png");
    auto skyHandle = ResourceManager::Instance().LoadTexture("Assets/Textures/IBL/skybox_highres.dds");
    if (irrHandle.IsValid()) m_irradianceMapSRV = ResourceManager::Instance().GetTexture(irrHandle);
    if (preHandle.IsValid()) m_prefilterMapSRV = ResourceManager::Instance().GetTexture(preHandle);
    if (lutHandle.IsValid()) m_brdfLutSRV = ResourceManager::Instance().GetTexture(lutHandle);
    if (skyHandle.IsValid()) m_defaultSkyboxSRV = ResourceManager::Instance().GetTexture(skyHandle);

    // フォグ用定数バッファ
    GpuResourceUtils::CreateDynamicConstantBuffer(
        Graphics::Instance().GetDevice(), sizeof(GpuData::CbFog), m_fogConstantBuffer.GetAddressOf());
}

std::vector<CCL::ECS::TypeID> EnvironmentSystem::GetReadTypes() const
{
    return {CCL::ECS::TypeInfo<FogComponent>::ID(),
            CCL::ECS::TypeInfo<TransformComponent>::ID()};
}

void EnvironmentSystem::Update(float /*dt*/)
{
    // 処理はすべて BindGlobalResources で行う（描画パスの直前に呼ばれる）
}

void EnvironmentSystem::BindGlobalResources(ID3D11DeviceContext *dc, const RenderServices &s)
{
    if (!m_world) return;

    // 1. CbScene (b8) / CbSkeleton (b12) をバインドする。
    //    中身は Graphics::Update() が書いた後なので、ここで書き換えてはいけない。
    if (m_world->HasResource<ModelConstantBuffers *>())
    {
        if (auto *renderer = m_world->GetResource<ModelConstantBuffers *>())
        {
            renderer->BindAllGlobalConstantBuffers(dc);
        }
    }

    // 2. グローバルノイズテクスチャ (t13)
    if (m_globalNoiseSRV)
    {
        ID3D11ShaderResourceView *srvs[] = {m_globalNoiseSRV.Get()};
        dc->PSSetShaderResources(SLOT_SRV_GLOBAL_NOISE, 1, srvs);
    }

    // 3. フォグ定数バッファ (b11)
    //   既定はフォグ無し。gFogColor.a=0 で、ジオメトリ（ApplyHeightFog）と
    //   スカイボックス（地平線フォグ）の両方のフォグが消える。
    //   FogComponent が無いか enabled=false なら、この既定のまま送る。
    GpuData::CbFog fogData = {{0.5f, 0.5f, 0.5f, 0.0f}, {0.0f, 1000.0f, 10.0f, 0.0f}, {}, {}, {}, {}};

    if (auto *fog = m_world->GetSingletonComponent<FogComponent>(); fog && fog->enabled)
    {
        fogData.gFogColor       = fog->color;
        fogData.gFogParams      = {fog->start, fog->end, fog->heightStart, fog->heightDensity};
        fogData.gFogCenter    = fog->center;
        fogData.gFogRimColor  = {fog->rimColor.x, fog->rimColor.y, fog->rimColor.z, 1.0f};
        fogData.gFogRimParams = {fog->rimPower, fog->rimStrength, 0.0f, 0.0f};
        fogData.gSkyFogParams = {fog->skyFogHeight, fog->skyFogSoftness, 0.0f, 0.0f};
    }

    if (m_fogConstantBuffer)
    {
        D3D11_MAPPED_SUBRESOURCE mapped;
        if (SUCCEEDED(dc->Map(m_fogConstantBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
        {
            memcpy(mapped.pData, &fogData, sizeof(GpuData::CbFog));
            dc->Unmap(m_fogConstantBuffer.Get(), 0);
        }
        dc->PSSetConstantBuffers(SLOT_CB_FOG, 1, m_fogConstantBuffer.GetAddressOf());
    }

    // =================================================================
    // 4. スカイボックスと IBL を決める
    //   SkyboxComponent（シングルトン）が空・IEM・PMREM の .dds パスを持つ。
    //   フォグのノイズと同じく、ハンドルが無効なときに読み込む。
    //   パスが空か読み込みに失敗したら、Initialize で用意した既定を使う。
    // =================================================================
    ID3D11ShaderResourceView *skyboxSRV     = m_defaultSkyboxSRV.Get();
    ID3D11ShaderResourceView *irradianceSRV = m_irradianceMapSRV.Get();
    ID3D11ShaderResourceView *prefilterSRV  = m_prefilterMapSRV.Get();

    if (auto *sky = m_world->GetSingletonComponent<SkyboxComponent>())
    {
        auto &rm = ResourceManager::Instance();

        // パスがあってハンドルが無効なら読み込む（パスを変えるとインスペクタがハンドルを無効にする）
        auto resolve = [&](char *path, TextureHandle &handle,
                           ID3D11ShaderResourceView *fallback) -> ID3D11ShaderResourceView * {
            if (path[0] == '\0') return fallback; // 空欄は既定
            if (!handle.IsValid()) handle = rm.LoadTexture(path);
            ID3D11ShaderResourceView *srv = handle.IsValid() ? rm.GetTexture(handle) : nullptr;
            return srv ? srv : fallback; // ロード失敗も既定へ
        };

        skyboxSRV     = resolve(sky->skyboxTexturePath, sky->skyboxHandle, m_defaultSkyboxSRV.Get());
        irradianceSRV = resolve(sky->irradiancePath, sky->irradianceHandle, m_irradianceMapSRV.Get());
        prefilterSRV  = resolve(sky->prefilterPath, sky->prefilterHandle, m_prefilterMapSRV.Get());
    }

    // 4a. IBL テクスチャ (t14 IEM, t15 PMREM, t16 BRDF LUT。LUT は空に依存しない)
    ID3D11ShaderResourceView *iblSRVs[3] = {irradianceSRV, prefilterSRV, m_brdfLutSRV.Get()};
    dc->PSSetShaderResources(SLOT_SRV_IBL_IRRADIANCE, 3, iblSRVs);

    // 4b. スカイボックスの背景は SkyboxPass が描くときに t17 へバインドする。ここでは Graphics に渡すだけ
    //     （SkyboxPass は World を参照できないので、MakeFrameContext 経由で渡す）。
    Graphics::Instance().SetSkyboxSRV(skyboxSRV);

    // 5. IBL 用サンプラ (s3)
    if (s.renderState)
    {
        ID3D11SamplerState *smp = s.renderState->GetSamplerState(SamplerState::LinearClamp);
        dc->PSSetSamplers(SLOT_SMP_IBL, 1, &smp);
    }
}

REGISTER_RENDER_SYSTEM(EnvironmentSystem, Priority::RenderStage::R02_Environment);