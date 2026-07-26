#include "AudioSystem.h"
#include "Engine/Audio/FmodIntegration/IAudioAPI.h"
#include "Engine/Audio/Components/AudioEvents.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Platform/Core/Logger.h" // 厳格なロギング仕様に従う
#include "Engine/Timer/TimeState.h"      // TimeContext（時間倍率→音のスロー演出）

#include <algorithm>
#include <cmath>

// 指示書通り、cppファイルのインクルード直後で using namespace を使用する
using namespace CCL::ECS;

void AudioSystem::Initialize() {
    // ---------------------------------------------------------
    // イベント購読 (ListenEvent)
    // 理由: 基底クラスの _eventCleanups に自動登録され、
    // システム破棄時に安全に Unsubscribe されるため。
    // ---------------------------------------------------------
    ListenEvent<PlaySoundEvent>([this](const PlaySoundEvent& e) {
        if (!_world) return;

        // リソースへのアクセス前に必ず HasResource でNullチェックを行う (指示書準拠)
        if (!_world->HasResource<std::shared_ptr<IAudioAPI>>()) return;

        auto& api = _world->GetResource<std::shared_ptr<IAudioAPI>>();
        if (api) {
            api->PlayOneShot3D(e.eventHash, e.position);
        }
        });
}

void AudioSystem::Update(float dt) {
    if (!_world) return;

    // リソースの生存確認
    if (!_world->HasResource<std::shared_ptr<IAudioAPI>>()) return;
    auto& api = _world->GetResource<std::shared_ptr<IAudioAPI>>();
    if (!api) return;

    // ---------------------------------------------------------
    // エミッターの座標同期 (並列処理)
    // 理由: チャンク単位のバッチ処理 (ForEachParallel) を用いることで、
    // メモリの連続性を活かし(SoA)、マルチコアでの高速処理を実現する。
    // ---------------------------------------------------------
    ForEachParallel([&api](const TransformComponent& transform, AudioEmitterComponent& emitter) {

        // 名前(eventName)が“正”。名前が変わったらハッシュを引き直す（インスペクタ/コードでの切替に対応）。
        // eventHash は実行時キャッシュ。0 のときだけ名前から導出する。名前を変えたい時は eventHash を 0 に戻す運用。
        if (emitter.eventHash == 0 && !emitter.eventName.empty())
            emitter.eventHash = CCL::Utils::HashString(emitter.eventName.c_str());

        // --- 音の切り替え：鳴らしている音(activeHash)と指定(eventHash)が食い違ったら止めて鳴らし直す ---
        if (emitter.isPlaying && emitter.playingId != 0 && emitter.eventHash != emitter.activeHash) {
            api->StopEvent(emitter.playingId);
            emitter.isPlaying = false;
            emitter.playingId = 0;
        }

        // --- 未再生で自動再生フラグが立っていれば開始 ---
        if (emitter.autoPlay && !emitter.isPlaying && emitter.eventHash != 0) {
            emitter.playingId = api->PlayEvent3D(emitter.eventHash, transform.position);
            emitter.isPlaying = (emitter.playingId != 0);
            if (emitter.isPlaying) {
                emitter.activeHash   = emitter.eventHash;
                api->SetEventParameter(emitter.playingId, "volume", emitter.volume);
                emitter.activeVolume = emitter.volume;
            }
        }

        // --- 再生中：音量の変化をライブ反映 ---
        if (emitter.isPlaying && emitter.playingId != 0 && emitter.volume != emitter.activeVolume) {
            api->SetEventParameter(emitter.playingId, "volume", emitter.volume);
            emitter.activeVolume = emitter.volume;
        }

        // --- 再生中：座標を毎フレーム追従 ---
        if (emitter.isPlaying && emitter.playingId != 0) {
            api->SetEvent3DAttributes(emitter.playingId, transform.position);
        }
        });

    // ---------------------------------------------------------
    // スロー演出：ゲームの時間倍率をそのまま音のスロー度へ流す。
    //   globalScale が 1 未満（ジャスト回避のヒットストップ・撃破スロー等）になると
    //   slow>0 になり、DXTKバックエンドが全再生音のピッチを下げてリバーブを掛ける。
    // ---------------------------------------------------------
    if (_world->HasResource<TimeContext>())
    {
        const float globalScale = _world->GetResource<TimeContext>().globalScale;
        const float slow        = std::clamp(1.0f - globalScale, 0.0f, 1.0f);
        api->SetGlobalSlow(slow);
    }

    // ---------------------------------------------------------
    // オーディオエンジン内部の更新とメモリのお掃除
    // ---------------------------------------------------------
    api->Update();
}

// ---------------------------------------------------------
// システムの自動登録マクロ (cppファイルの末尾)
// 理由: Transformが更新された後（L04_Physics）に音源位置を同期させるため、
// LogicStageの後半である L05 または L06 以降がアーキテクチャ上適切。
// ---------------------------------------------------------
REGISTER_LOGIC_SYSTEM(AudioSystem, Priority::LogicStage::L06_Resolution);