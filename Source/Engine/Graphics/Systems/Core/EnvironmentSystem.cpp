#include "EnvironmentSystem.h"

#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/Graphics/Renderer/ModelRenderer.h"
#include "Engine/Graphics/Resource/ResourceManager.h"
#include "Engine/Graphics/Shader/ShaderResources.h"
#include "Engine/Graphics/Shader/ShaderShared.h"
#include "Engine/Graphics/RHI/GpuResourceUtils.h"

// TODO: ECS に Initialize(device) が追加されたら除去する
#include "Engine/Graphics/RHI/Graphics.h"

#include "Engine/Graphics/Components/Lighting/FogComponent.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Game/Core/SystemPriority.h"

EnvironmentSystem::EnvironmentSystem() : SystemBase("EnvironmentSystem")
{
    // グローバルノイズテクスチャ
    auto noiseHandle = ResourceManager::Instance().LoadTexture("Assets/Textures/Shader/Noise.png");
    if (noiseHandle.IsValid())
        _globalNoiseSRV = ResourceManager::Instance().GetTexture(noiseHandle);

    // IBL テクスチャ
    auto irrHandle = ResourceManager::Instance().LoadTexture("Assets/Textures/IBL/ibl_sky_iem.dds");
    auto preHandle = ResourceManager::Instance().LoadTexture("Assets/Textures/IBL/ibl_sky_pmrem.dds");
    auto lutHandle = ResourceManager::Instance().LoadTexture("Assets/Textures/IBL/ibl_brdf_lut.png");
    if (irrHandle.IsValid()) _irradianceMapSRV = ResourceManager::Instance().GetTexture(irrHandle);
    if (preHandle.IsValid()) _prefilterMapSRV = ResourceManager::Instance().GetTexture(preHandle);
    if (lutHandle.IsValid()) _brdfLutSRV = ResourceManager::Instance().GetTexture(lutHandle);

    // フォグ用定数バッファ
    // TODO: ECS に Initialize(device) が追加されたら除去する
    GpuResourceUtils::CreateDynamicConstantBuffer(
        Graphics::Instance().GetDevice(), sizeof(GpuData::CbFog), _fogConstantBuffer.GetAddressOf());
}

std::vector<CCL::ECS::TypeID> EnvironmentSystem::GetReadTypes() const
{
    return { CCL::ECS::TypeInfo<FogComponent>::ID(),
             CCL::ECS::TypeInfo<TransformComponent>::ID() };
}

void EnvironmentSystem::Update(float /*dt*/)
{
    // EnvironmentSystem::Update はフォグ・IBL コンポーネントの読み取りのみ担当。
    // CbScene（カメラ行列・ライト情報）は Graphics::Update() が管理するため、
    // ここでは一切触れない。
}

void EnvironmentSystem::BindGlobalResources(ID3D11DeviceContext* dc, const RenderServices& s)
{
    if (!_world) return;

    // =================================================================
    // ★ Bug Fix: CbScene の UpdateSubresource をここから削除した。
    //   Graphics::Update() が正しい行列データを書き込んだ後、
    //   EnvironmentSystem が古い / 未更新の _sceneData で上書きすると
    //   InstanceData がゼロ、CbScene の行列が壊れる等の描画バグを引き起こす。
    //   ここでは BindAllGlobalConstantBuffers でバインドするだけ。
    // =================================================================

    // 1. CbScene (b8) / CbSkeleton (b12) をパイプラインにバインドする
    if (_world->HasResource<ModelRenderer*>()) {
        if (auto* renderer = _world->GetResource<ModelRenderer*>()) {
            renderer->BindAllGlobalConstantBuffers(dc);
        }
    }

    // 2. グローバルノイズテクスチャ (t13)
    if (_globalNoiseSRV) {
        ID3D11ShaderResourceView* srvs[] = { _globalNoiseSRV.Get() };
        dc->PSSetShaderResources(SLOT_SRV_GLOBAL_NOISE, 1, srvs);
    }

    // 3. フォグ定数バッファ (b11)
    GpuData::CbFog fogData = { {0.5f,0.5f,0.5f,1.0f},{0.0f,1000.0f,10.0f,0.0f},{},{},{},{} };
    ID3D11ShaderResourceView* fogNoiseSrv = nullptr;

    auto fogView = _world->View<FogComponent>();
    if (!fogView.empty()) {
        if (auto* fog = _world->GetComponent<FogComponent>(fogView[0])) {
            fogData.gFogColor = fog->color;
            fogData.gFogParams = { fog->start, fog->end, fog->heightStart, fog->heightDensity };
            fogData.gFogCenter = fog->center;
            fogData.gFogNoiseParams = { fog->noiseScale, fog->noiseStrength,
                                    fog->noiseSpeed.x, fog->noiseSpeed.y };
            fogData.gFogRimColor = { fog->rimColor.x, fog->rimColor.y, fog->rimColor.z, 1.0f };
            fogData.gFogRimParams = { fog->rimPower, fog->rimStrength, 0.0f, 0.0f };
            fogNoiseSrv = fog->noiseTextureSRV.Get();
        }
    }

    if (_fogConstantBuffer) {
        D3D11_MAPPED_SUBRESOURCE mapped;
        if (SUCCEEDED(dc->Map(_fogConstantBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
            memcpy(mapped.pData, &fogData, sizeof(GpuData::CbFog));
            dc->Unmap(_fogConstantBuffer.Get(), 0);
        }
        dc->PSSetConstantBuffers(SLOT_CB_FOG, 1, _fogConstantBuffer.GetAddressOf());
    }
    dc->PSSetShaderResources(SLOT_SRV_FOG_NOISE, 1, &fogNoiseSrv);


    // 4. IBL テクスチャ (t14, t15, t16)
    ID3D11ShaderResourceView* iblSRVs[3] = { nullptr, nullptr, nullptr };
    if (_irradianceMapSRV) iblSRVs[0] = _irradianceMapSRV.Get();
    if (_prefilterMapSRV)  iblSRVs[1] = _prefilterMapSRV.Get();
    if (_brdfLutSRV)       iblSRVs[2] = _brdfLutSRV.Get();
    dc->PSSetShaderResources(SLOT_SRV_IBL_IRRADIANCE, 3, iblSRVs);

    // 5. IBL 用サンプラ (s3)
    if (s.renderState) {
        ID3D11SamplerState* smp = s.renderState->GetSamplerState(SamplerState::LinearClamp);
        dc->PSSetSamplers(SLOT_SMP_IBL, 1, &smp);
    }
}

REGISTER_RENDER_SYSTEM(EnvironmentSystem, Priority::RenderStage::R02_Environment);