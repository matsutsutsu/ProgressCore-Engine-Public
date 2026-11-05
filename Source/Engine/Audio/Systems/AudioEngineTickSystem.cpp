#include "AudioEngineTickSystem.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include "Engine/Audio/IAudioAPI.h"

#include <memory>

void AudioEngineTickSystem::Update(float dt)
{
    if (!m_world) return;

    if (!m_world->HasResource<std::shared_ptr<IAudioAPI>>()) return;
    auto &api = m_world->GetResource<std::shared_ptr<IAudioAPI>>();
    if (!api) return;

    // dt は描画フレームの経過時間（固定ステップの dt ではない）
    api->Update(dt);
}



// 発音の指示（AudioSystem / Logic）とリスナー位置（CameraListenerSystem / R02）が揃った後に回す
REGISTER_RENDER_SYSTEM(AudioEngineTickSystem, Priority::RenderStage::R11_AudioTick);
