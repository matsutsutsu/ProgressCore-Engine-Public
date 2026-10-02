#include "AudioSystem.h"
#include "Engine/Audio/IAudioAPI.h"
#include "Engine/Audio/Components/AudioEvents.h"
#include "Engine/Animation/Core/AnimEventMessages.h" // アニメーションの Sound イベント
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Time/TimeState.h" // TimeContext（時間倍率 → 音のスロー演出）

#include <algorithm>

using namespace CCL::ECS;

void AudioSystem::Initialize()
{
    // ListenEvent で購読すると、System の破棄時に購読解除まで自動で行われる
    ListenEvent<PlaySoundEvent>([this](const PlaySoundEvent &e) {
        if (!m_world) return;
        if (!m_world->HasResource<std::shared_ptr<IAudioAPI>>()) return;

        auto &api = m_world->GetResource<std::shared_ptr<IAudioAPI>>();
        if (api)
        {
            api->PlayOneShot3D(e.eventHash, e.position);
        }
    });

    // アニメーションの Sound イベント（AnimationSystem が L02 で Publish する）。
    //   鳴らす位置はその瞬間の entity のワールド位置で、以降は追従しない
    ListenEvent<AnimEventSoundMessage>([this](const AnimEventSoundMessage &e) {
        if (!m_world || e.eventPath.empty()) return;
        if (!m_world->HasResource<std::shared_ptr<IAudioAPI>>()) return;

        auto &api = m_world->GetResource<std::shared_ptr<IAudioAPI>>();
        if (!api) return;

        const auto *transform = m_world->GetComponent<TransformComponent>(e.entity);
        if (!transform) return;

        // アニメのイベントは数フレームに 1 回程度なので、ハッシュは毎回計算する
        api->PlayOneShot3D(CCL::Utils::HashString(e.eventPath.c_str()), transform->GetWorldPosition(), e.volume, e.pitch);
    });
}

void AudioSystem::Update(float dt)
{
    if (!m_world) return;

    if (!m_world->HasResource<std::shared_ptr<IAudioAPI>>()) return;
    auto &api = m_world->GetResource<std::shared_ptr<IAudioAPI>>();
    if (!api) return;

    // エミッター同士は独立なので並列に回す（DxtkAudioManager 側は mutex で保護されている）
    ForEachParallel([&api](const TransformComponent &transform, AudioEmitterComponent &emitter) {
        // eventHash は eventName のキャッシュで、0 のときだけ名前から導出する。
        //   インスペクタは名前と同時にハッシュも更新する。コードから名前を変えるときは eventHash を 0 に戻すこと
        if (emitter.eventHash == 0 && !emitter.eventName.empty())
            emitter.eventHash = CCL::Utils::HashString(emitter.eventName.c_str());

        // 鳴らしている音（activeHash）や 2D/3D が設定と食い違ったら、止めて下で鳴らし直す
        if (emitter.isPlaying && emitter.playingId != 0 && (emitter.eventHash != emitter.activeHash || emitter.is3D != emitter.activeIs3D))
        {
            api->StopEvent(emitter.playingId);
            emitter.isPlaying = false;
            emitter.playingId = 0;
        }

        // 未再生で autoPlay なら再生を始める
        if (emitter.autoPlay && !emitter.isPlaying && emitter.eventHash != 0)
        {
            emitter.playingId = emitter.is3D
                                    ? api->PlayEvent3D(emitter.eventHash, transform.position)
                                    : api->PlayEvent(emitter.eventHash);
            emitter.isPlaying = (emitter.playingId != 0);
            if (emitter.isPlaying)
            {
                emitter.activeHash = emitter.eventHash;
                emitter.activeIs3D = emitter.is3D;
                api->SetEventParameter(emitter.playingId, "volume", emitter.volume);
                emitter.activeVolume = emitter.volume;
            }
        }

        // 再生中に音量が変わったら反映する
        if (emitter.isPlaying && emitter.playingId != 0 && emitter.volume != emitter.activeVolume)
        {
            api->SetEventParameter(emitter.playingId, "volume", emitter.volume);
            emitter.activeVolume = emitter.volume;
        }

        // 3D のときだけ発音位置を追従させる（2D は位置に依存しない）
        if (emitter.isPlaying && emitter.playingId != 0 && emitter.is3D)
        {
            api->SetEvent3DAttributes(emitter.playingId, transform.position);
        }
    });

    // ゲームの時間倍率をそのまま音のスロー度にする。
    //   globalScale が 1 未満（ジャスト回避・撃破スロー等）になると slow>0 になり、
    //   DxtkAudioManager が全再生音のピッチを下げてリバーブを掛ける
    if (m_world->HasResource<TimeContext>())
    {
        const float globalScale = m_world->GetResource<TimeContext>().globalScale;
        const float slow        = std::clamp(1.0f - globalScale, 0.0f, 1.0f);
        api->SetGlobalSlow(slow);
    }

    // api->Update はここでは呼ばない（AudioEngineTickSystem が 1 描画フレームに 1 回呼ぶ）
}

// 物理（L04_Physics）で Transform が確定した後に音源位置を同期する
REGISTER_LOGIC_SYSTEM(AudioSystem, Priority::LogicStage::L06_Resolution);