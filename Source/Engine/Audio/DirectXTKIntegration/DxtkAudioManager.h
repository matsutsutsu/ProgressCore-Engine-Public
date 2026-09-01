#pragma once
// DirectXTK Audio(XAudio2) を用いた IAudioAPI の実装。FMOD の差し替え先。
//
// 設計方針:
//   - 既存の IAudioAPI 抽象をそのまま実装するので、ゲーム/ECS(AudioSystem 等)は無変更。
//   - FMOD Studio の Bank/Event を DXTK 向けに読み替える:
//       * Bank            → 概念なし（LoadBank は no-op）
//       * Event(event:/x) → WAVファイル。AudioConfigDXTK.json で「イベント名→wavパス」を対応させ、
//                           起動時に SoundEffect としてロード（イベント名のハッシュを key に保持）。
//   - スロー演出: SetGlobalSlow(0..1) で全再生インスタンスのピッチを下げ、スロー中はリバーブを掛ける。
//                 これでジャスト回避のスロー時間に音も“ｳﾞｫﾜｰﾝ”と遅くなる。
//   - 実装詳細(XAudio2依存)は Impl(pimpl) に隠し、このヘッダは xaudio2 を持ち込まない。
//   - AudioSystem が ForEachParallel から呼ぶため、内部マップは mutex で保護する。
//   - 注意: DXTK は WAV のみ再生可能（mp3/ogg 不可）。ファイルが無い場合は警告してスキップする。
#include "Engine/Audio/IAudioAPI.h"
#include <memory>
#include <string>
#include <vector>

class DxtkAudioManager : public IAudioAPI
{
  public:
    DxtkAudioManager();
    ~DxtkAudioManager() override;

    // 基底の const char* 版ヘルパーが uint32_t 版の override で隠蔽されるのを防ぐ。
    using IAudioAPI::PlayOneShot;
    using IAudioAPI::PlayOneShot3D;
    using IAudioAPI::PlayEvent;

    // ---- ライフサイクル ----
    void Initialize() override;
    void Update() override;

    // ---- データロード ----
    void LoadConfig(const std::string &configFilePath) override;
    void LoadBank(const char *bankFilePath) override;   // DXTKでは不要（no-op）
    void LoadEvent(const char *eventPath) override;      // configで一括ロードするため no-op
    void FinalizeEventLoading() override;                // no-op

    // ---- ワンショット ----
    void PlayOneShot(uint32_t eventHash) override;
    void PlayOneShot3D(uint32_t eventHash, const DirectX::XMFLOAT3 &position) override;

    // ---- 継続再生と制御 ----
    uint32_t PlayEvent(uint32_t eventHash) override;
    uint32_t PlayEvent3D(uint32_t eventHash, const DirectX::XMFLOAT3 &position) override;
    void StopEvent(uint32_t playingId, bool allowFadeOut = true) override;
    void StopAll() override; // 継続再生・3Dワンショットを全停止（シーン終了時の確実な後始末）
    void SetEventParameter(uint32_t playingId, const char *paramName, float value) override;
    void SetEvent3DAttributes(uint32_t playingId, const DirectX::XMFLOAT3 &pos) override;

    // ---- 3Dリスナー ----
    void SetListenerPosition(const DirectX::XMFLOAT3 &pos, const DirectX::XMFLOAT3 &forward,
        const DirectX::XMFLOAT3 &up) override;

    // ---- スロー演出（ジャスト回避など。0=通常, 1=最スロー） ----
    void SetGlobalSlow(float slow01) override;

    // ---- エディタ支援：ロード済みイベント名の一覧（インスペクタのドロップダウン用） ----
    std::vector<std::string> GetEventNames() const override;

  private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
