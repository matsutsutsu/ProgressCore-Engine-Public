#include "EnvironmentSystem.h"

#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/Graphics/Renderer/ModelRenderer.h"
#include "Engine/Graphics/Resource/ResourceManager.h"
#include "Engine/Graphics/Shader/ShaderResources.h"
#include "Engine/Graphics/RHI/GpuResourceUtils.h"
#include "Engine/Graphics/RHI/Light.h"
#include "Engine/Graphics/RHI/Camera.h"

// コンストラクタでの device 取得にのみ使用（ECS 構造上、constructor に device を渡せないため残存）
// TODO: ECS の Initialize(device) パターンが整備されたら除去する
#include "Engine/Graphics/RHI/Graphics.h"

#include "Engine/Camera/Components/VirtualCameraComponents.h"
#include "Engine/Graphics/Components/Lighting/FogComponent.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Game/Core/SystemPriority.h"

using namespace DirectX;

EnvironmentSystem::EnvironmentSystem() : SystemBase("EnvironmentSystem")
{
    TextureHandle noiseTexHandle =
        ResourceManager::Instance().LoadTexture("Assets/Textures/Shader/Noise.png");
    if (noiseTexHandle.IsValid())
        _globalNoiseSRV = ResourceManager::Instance().GetTexture(noiseTexHandle);

    auto irrHandle = ResourceManager::Instance().LoadTexture("Assets/Textures/IBL/ibl_sky_iem.dds");
    auto preHandle = ResourceManager::Instance().LoadTexture("Assets/Textures/IBL/ibl_sky_pmrem.dds");
    auto lutHandle = ResourceManager::Instance().LoadTexture("Assets/Textures/IBL/ibl_brdf_lut.png");
    if (irrHandle.IsValid()) _irradianceMapSRV = ResourceManager::Instance().GetTexture(irrHandle);
    if (preHandle.IsValid()) _prefilterMapSRV = ResourceManager::Instance().GetTexture(preHandle);
    if (lutHandle.IsValid()) _brdfLutSRV = ResourceManager::Instance().GetTexture(lutHandle);

    // TODO: ECS が Initialize(device) を持つようになったら Graphics::Instance() を除去する
    GpuResourceUtils::CreateDynamicConstantBuffer(
        Graphics::Instance().GetDevice(), sizeof(CbFogData), _fogConstantBuffer.GetAddressOf());
}

std::vector<CCL::ECS::TypeID> EnvironmentSystem::GetReadTypes() const
{
    return { CCL::ECS::TypeInfo<VirtualCamera>::ID(),
             CCL::ECS::TypeInfo<FogComponent>::ID(),
             CCL::ECS::TypeInfo<TransformComponent>::ID() };
}

void EnvironmentSystem::Update(float dt)
{
    if (!_world || !_world->HasResource<ModelRenderer*>()) return;
    ModelRenderer* renderer = _world->GetResource<ModelRenderer*>();
    if (!renderer) return;

    if (_world->HasResource<Camera*>()) {
        Camera* mainCamera = _world->GetResource<Camera*>();
        if (mainCamera) {
            DirectX::XMMATRIX V = DirectX::XMLoadFloat4x4(&mainCamera->GetView());
            DirectX::XMMATRIX P = DirectX::XMLoadFloat4x4(&mainCamera->GetProjection());

            DirectX::XMFLOAT4X4 viewMat, projMat, viewProjMat;
            DirectX::XMStoreFloat4x4(&viewMat, V);
            DirectX::XMStoreFloat4x4(&projMat, P);
            DirectX::XMStoreFloat4x4(&viewProjMat, V * P);

            _sceneData.viewProjection = viewProjMat;
            _sceneData.view = viewMat;
            _sceneData.projection = projMat;

            DirectX::XMFLOAT3 eye = mainCamera->GetEye();
            _sceneData.cameraPosition = { eye.x, eye.y, eye.z, 1.0f };
        }
    }

    if (_world->HasResource<LightManager*>()) {
        auto* lightManager = _world->GetResource<LightManager*>();
        if (lightManager) {
            const auto& dir = lightManager->GetDirectionalLight();
            _sceneData.lightDirection = { dir.direction.x, dir.direction.y, dir.direction.z, 0.0f };
            _sceneData.lightColor = { dir.color.x, dir.color.y, dir.color.z, 1.0f };
        }
    }
}

void EnvironmentSystem::BindGlobalResources(ID3D11DeviceContext* dc, const RenderServices& s)
{
    if (!_world) return;

    // =========================================================
    // 1. CbScene の GPU 転送とバインド
    //    Graphics::Instance() → s.sceneConstantBuffer に変更（singleton 除去）
    // =========================================================
    if (s.sceneConstantBuffer) {
        dc->UpdateSubresource(s.sceneConstantBuffer, 0, nullptr, &_sceneData, 0, 0);
    }

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

    // 3. フォグ定数バッファ
    CbFogData fogData = { {0.5f,0.5f,0.5f,1.0f},{0.0f,1000.0f,10.0f,0.0f},{},{},{},{} };
    ID3D11ShaderResourceView* fogNoiseSrv = nullptr;

    auto fogView = _world->View<FogComponent>();
    if (!fogView.empty()) {
        if (auto* fog = _world->GetComponent<FogComponent>(fogView[0])) {
            fogData.color = fog->color;
            fogData.params = { fog->start, fog->end, fog->heightStart, fog->heightDensity };
            fogData.center = fog->center;
            fogData.noiseParams = { fog->noiseScale, fog->noiseStrength,
                                    fog->noiseSpeed.x, fog->noiseSpeed.y };
            fogData.rimColor = { fog->rimColor.x, fog->rimColor.y, fog->rimColor.z, 1.0f };
            fogData.rimParams = { fog->rimPower, fog->rimStrength, 0.0f, 0.0f };
            fogNoiseSrv = fog->noiseTextureSRV.Get();
        }
    }

    if (_fogConstantBuffer) {
        D3D11_MAPPED_SUBRESOURCE mapped;
        if (SUCCEEDED(dc->Map(_fogConstantBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
            memcpy(mapped.pData, &fogData, sizeof(CbFogData));
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
    //    Graphics::Instance().GetRenderState() → s.renderState に変更（singleton 除去）
    if (s.renderState) {
        ID3D11SamplerState* iblSampler =
            s.renderState->GetSamplerState(SamplerState::LinearClamp);
        dc->PSSetSamplers(SLOT_SMP_IBL, 1, &iblSampler);
    }
}

REGISTER_RENDER_SYSTEM(EnvironmentSystem, Priority::RenderStage::R02_Environment);