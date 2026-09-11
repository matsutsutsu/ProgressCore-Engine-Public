#include "AudioSystem.h"
#include "Engine/Audio/IAudioAPI.h"
#include "Engine/Audio/Components/AudioEvents.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Platform/Core/Logger.h" // 厳格なロギング仕様に従う
#include "Engine/Time/TimeState.h"      // TimeContext（時間倍率→音のスロー演出）

#include <algorithm>
#include <cmath>

// 指示書通り、cppファイルのインクルード直後で using namespace を使用する
using namespace CCL::ECS;

void AudioSystem::Initialize() {
    // ---------------------------------------------------------
    // イベント購読 (ListenEvent)
    // 理由: 基底クラスの m_eventCleanups に自動登録され、
    // システム破棄時に安全に Unsubscribe されるため。
    // ---------------------------------------------------------
    ListenEvent<PlaySoundEvent>([this](const PlaySoundEvent& e) {
        if (!m_world) return;

        // リソースへのアクセス前に必ず HasResource でNullチェックを行う (指示書準拠)
        if (!m_world->HasResource<std::shared_ptr<IAudioAPI>>()) return;

        auto& api = m_world->GetResource<std::shared_ptr<IAudioAPI>>();
        if (api) {
            api->PlayOneShot3D(e.eventHash, e.position);
        }
        });
}

void AudioSystem::Update(float dt) {
    if (!m_world) return;

    // リソースの生存確認
    if (!m_world->HasResource<std::shared_ptr<IAudioAPI>>()) return;
    auto& api = m_world->GetResource<std::shared_ptr<IAudioAPI>>();
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

        // --- 音の切り替え：鳴らしている音(activeHash)や空間モード(2D/3D)が指定と食い違ったら止めて鳴らし直す ---
        if (emitter.isPlaying && emitter.playingId != 0
            && (emitter.eventHash != emitter.activeHash || emitter.is3D != emitter.activeIs3D)) {
            api->StopEvent(emitter.playingId);
            emitter.isPlaying = false;
            emitter.playingId = 0;
        }

        // --- 未再生で自動再生フラグが立っていれば開始（is3D で 3D空間音／2D一様再生を選ぶ） ---
        if (emitter.autoPlay && !emitter.isPlaying && emitter.eventHash != 0) {
            emitter.playingId = emitter.is3D
                ? api->PlayEvent3D(emitter.eventHash, transform.position)
                : api->PlayEvent(emitter.eventHash); // 2D: 距離減衰・定位なし（BGM向き）
            emitter.isPlaying = (emitter.playingId != 0);
            if (emitter.isPlaying) {
                emitter.activeHash   = emitter.eventHash;
                emitter.activeIs3D   = emitter.is3D;
                api->SetEventParameter(emitter.playingId, "volume", emitter.volume);
                emitter.activeVolume = emitter.volume;
            }
        }

        // --- 再生中：音量の変化をライブ反映 ---
        if (emitter.isPlaying && emitter.playingId != 0 && emitter.volume != emitter.activeVolume) {
            api->SetEventParameter(emitter.playingId, "volume", emitter.volume);
            emitter.activeVolume = emitter.volume;
        }

        // --- 再生中：3Dのときだけ座標を毎フレーム追従（2Dは位置に依存しないので不要） ---
        if (emitter.isPlaying && emitter.playingId != 0 && emitter.is3D) {
            api->SetEvent3DAttributes(emitter.playingId, transform.position);
        }
        });

    // ---------------------------------------------------------
    // スロー演出：ゲームの時間倍率をそのまま音のスロー度へ流す。
    //   globalScale が 1 未満（ジャスト回避のヒットストップ・撃破スロー等）になると
    //   slow>0 になり、DXTKバックエンドが全再生音のピッチを下げてリバーブを掛ける。
    // ---------------------------------------------------------
    if (m_world->HasResource<TimeContext>())
    {
        const float globalScale = m_world->GetResource<TimeContext>().globalScale;
        const float slow        = std::clamp(1.0f - globalScale, 0.0f, 1.0f);
        api->SetGlobalSlow(slow);
    }

    // オーディオエンジン内部の更新(api->Update)はここでは呼ばない。
    //   本システムは Logic(FixedUpdate) なので1描画フレームに最大4回まわる
    //   （Framework::MAX_STEPS）が、DXTK の AudioEngine::Update は
    //   「毎フレーム1回」を前提にしたAPIで、内部の追従補間もその回数に引きずられる。
    //   エンジンの1フレーム更新は AudioEngineTickSystem（Render / R11_AudioTick）が担う。
}

// ---------------------------------------------------------
// システムの自動登録マクロ (cppファイルの末尾)
// 理由: Transformが更新された後（L04_Physics）に音源位置を同期させるため、
// LogicStageの後半である L05 または L06 以降がアーキテクチャ上適切。
// ---------------------------------------------------------
REGISTER_LOGIC_SYSTEM(AudioSystem, Priority::LogicStage::L06_Resolution);