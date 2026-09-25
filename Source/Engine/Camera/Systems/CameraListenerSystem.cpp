#include "CameraListenerSystem.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/Graphics/RHI/Camera.h"
#include "Engine/Audio/IAudioAPI.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

#include <SimpleMath.h>
using namespace DirectX::SimpleMath;

CameraListenerSystem::CameraListenerSystem() : SystemBase("CameraListenerSystem") {}

void CameraListenerSystem::Update(float dt)
{
    if (!m_world) return;

    if (!m_world->HasResource<Camera *>() || !m_world->HasResource<std::shared_ptr<IAudioAPI>>()) return;

    Camera *mainCamera = m_world->GetResource<Camera *>();
    auto   &audioAPI   = m_world->GetResource<std::shared_ptr<IAudioAPI>>();

    if (!mainCamera || !audioAPI) return;

    Vector3 pos    = mainCamera->GetEye();
    Vector3 lookAt = mainCamera->GetFocus();
    Vector3 up     = mainCamera->GetUp();

    Vector3 forward = lookAt - pos;
    forward.Normalize();

    // DXTK Audio の AudioListener へ渡す
    audioAPI->SetListenerPosition(
        {pos.x, pos.y, pos.z},
        {forward.x, forward.y, forward.z},
        {up.x, up.y, up.z});
}

// CameraBrainSystem(R01b) がメインカメラを確定した後に読む
REGISTER_RENDER_SYSTEM(CameraListenerSystem, Priority::RenderStage::R02_Environment);