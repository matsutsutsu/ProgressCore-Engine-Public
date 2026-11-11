#pragma once
// ===========================================================================
// 概要: AudioEmitterComponent の再生状態を IAudioAPI へ反映する System（L06）
//
// 責務:
//   - AudioEmitter の自動再生・音の切り替え・音量変更・3D 位置の追従
//   - PlaySoundEvent / AnimEventSoundMessage（アニメの Sound イベント）を受けてワンショットの 3D 効果音を鳴らす
//   - TimeContext の時間倍率を音のスロー度として IAudioAPI へ渡す
//
// 備考: エンジン自体の更新（IAudioAPI::Update）は呼ばない。理由は AudioEngineTickSystem.h を参照
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/ECS/System/CCL_SystemAccess.h"
#include "Engine/Audio/Components/AudioEmitterComponent.h"
#include "Engine/Transform/Components/TransformComponent.h"

class IAudioAPI;

class AudioSystem : public CCL::ECS::IfSystem<
    AudioSystem,
    CCL::ECS::Read<TransformComponent>,
    CCL::ECS::Write<AudioEmitterComponent>>
{
public:
    AudioSystem() : IfSystem("AudioSystem") {}
    virtual ~AudioSystem() = default;

    void Initialize() override;

    void Update(float dt) override;
};