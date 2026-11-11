#include "LightSystem.h"

#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/Graphics/SceneView/Light.h"

#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

#include "Engine/Graphics/Components/Lighting/LightComponent.h"
#include "Engine/Transform/Components/TransformComponent.h"

std::vector<CCL::ECS::TypeID> LightSystem::GetReadTypes() const
{
    return {CCL::ECS::TypeInfo<DirectionalLightComponent>::ID(),
            CCL::ECS::TypeInfo<PointLightComponent>::ID(),
            CCL::ECS::TypeInfo<SpotLightComponent>::ID(),
            CCL::ECS::TypeInfo<TransformComponent>::ID()};
}



void LightSystem::Update(float dt)
{
    if (!m_world || !m_world->HasResource<LightManager *>()) return;
    LightManager *lightManager = m_world->GetResource<LightManager *>();
    if (!lightManager) return;

    // 1. 毎フレーム空にしてから積み直す
    //    Clear と積み込みを別の System に分けると順番が保証されないので、1つの System で行う
    lightManager->Clear();

    // 2. 平行光源
    auto dirView = m_world->View<DirectionalLightComponent, TransformComponent>();
    for (auto entity : dirView)
    {
        auto *lightComp = m_world->GetComponent<DirectionalLightComponent>(entity);
        auto *transComp = m_world->GetComponent<TransformComponent>(entity);

        if (!lightComp || !transComp) continue;

        DirectionalLight gfxLight;
        gfxLight.color     = lightComp->color;
        gfxLight.intensity = lightComp->intensity;

        // 半球ライティングの空と地面の色
        gfxLight.gSkyColor    = lightComp->skyColor;
        gfxLight.gGroundColor = lightComp->groundColor;

        gfxLight.direction = CalculateDirection(transComp->rotation);

        lightManager->SetDirectionalLight(gfxLight);
    }

    // 3. 点光源
    auto pointView = m_world->View<PointLightComponent, TransformComponent>();
    for (auto entity : pointView)
    {
        auto *lightComp = m_world->GetComponent<PointLightComponent>(entity);
        auto *transComp = m_world->GetComponent<TransformComponent>(entity);

        if (!lightComp || !transComp) continue;

        // GpuData の構造体は HLSL と共有なので既定値を持てない。{} でゼロにしてから全項目を入れる
        PointLights gfxLight{};
        gfxLight.active               = 1.0f; // シェーダーは 0.5 未満を無効として飛ばす
        gfxLight.color                = lightComp->color;
        gfxLight.intensity            = lightComp->intensity;
        gfxLight.range                = lightComp->range;
        gfxLight.constantAttenuation  = lightComp->constant;
        gfxLight.linearAttenuation    = lightComp->linear;
        gfxLight.quadraticAttenuation = lightComp->quadratic;

        gfxLight.position = transComp->GetWorldPosition();

        lightManager->AddPointLight(gfxLight);
    }

    // 4. スポットライト
    auto spotView = m_world->View<SpotLightComponent, TransformComponent>();
    for (auto entity : spotView)
    {
        auto *lightComp = m_world->GetComponent<SpotLightComponent>(entity);
        auto *transComp = m_world->GetComponent<TransformComponent>(entity);

        if (!lightComp || !transComp) continue;

        SpotLights gfxLight{};
        gfxLight.active               = 1.0f;
        gfxLight.color                = lightComp->color;
        gfxLight.intensity            = lightComp->intensity;
        gfxLight.range                = lightComp->range;
        gfxLight.innerCos             = lightComp->innerCos;
        gfxLight.outerCos             = lightComp->outerCos;
        gfxLight.constantAttenuation  = lightComp->constant;
        gfxLight.linearAttenuation    = lightComp->linear;
        gfxLight.quadraticAttenuation = lightComp->quadratic;

        gfxLight.position  = transComp->GetWorldPosition();
        gfxLight.direction = CalculateDirection(transComp->rotation);

        lightManager->AddSpotLight(gfxLight);
    }

}



DirectX::XMFLOAT3 LightSystem::CalculateDirection(const DirectX::XMFLOAT4 &rotation)
{
    using namespace DirectX;
    XMVECTOR q       = XMLoadFloat4(&rotation);
    XMVECTOR forward = XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);
    XMVECTOR dir     = XMVector3Rotate(forward, q);

    DirectX::XMFLOAT3 result;
    XMStoreFloat3(&result, dir);
    return result;
}



REGISTER_RENDER_SYSTEM(LightSystem, Priority::RenderStage::R02_Environment);