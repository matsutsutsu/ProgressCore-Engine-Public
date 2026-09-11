#include "AudioEngineTickSystem.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include "Engine/Audio/IAudioAPI.h"

#include <memory>

AudioEngineTickSystem::AudioEngineTickSystem() : SystemBase("AudioEngineTickSystem") {}

void AudioEngineTickSystem::Update(float dt)
{
    if (!m_world) return;

    if (!m_world->HasResource<std::shared_ptr<IAudioAPI>>()) return;
    auto &api = m_world->GetResource<std::shared_ptr<IAudioAPI>>();
    if (!api) return;

    // 1描画フレームにつきここだけが呼ぶ。dt は可変フレーム時間（固定ステップではない）。
    api->Update(dt);
}

// Render の最後段。発音指示(AudioSystem/Logic)とリスナー位置(CameraListenerSystem/R02)が
// 出揃った後に1回だけエンジンを回す。
REGISTER_RENDER_SYSTEM(AudioEngineTickSystem, Priority::RenderStage::R11_AudioTick);
