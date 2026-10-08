#pragma once
// ===========================================================================
// 概要: DirectXTK Audio（XAudio2）による IAudioAPI の実装 DxtkAudioManager。
//
// 責務:
//   - AudioConfigDXTK.json の「イベント名 → 音源ファイル」を起動時に SoundEffect としてロードする
//     （キーはイベント名のハッシュ）
//   - 2D/3D の再生、再生中インスタンスの制御、3D リスナーの更新
//   - スロー演出: SetGlobalSlow(0..1) で全再生音のピッチを下げ、スロー中はリバーブを掛ける
//
// 備考:
//   - WAV は DXTK が直接読む。mp3/wma/aac は Media Foundation で PCM にデコードしてから渡す。
//     読めないファイルは警告を出してスキップする。
//   - XAudio2 に依存する実装は Impl（pimpl）に隠し、このヘッダに xaudio2 を持ち込まない。
//   - AudioSystem が ForEachParallel の中から呼ぶため、内部のマップは mutex で保護する。
// ===========================================================================
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
    using IAudioAPI::PlayEvent;
    using IAudioAPI::PlayOneShot;
    using IAudioAPI::PlayOneShot3D;

    // ===========================================================================
    // ライフサイクル
    // ===========================================================================
    void Initialize() override;
    void Update(float dt) override;

    // ===========================================================================
    // データロード
    // ===========================================================================
    void LoadConfig(const std::string &configFilePath) override;

    // ===========================================================================
    // ワンショット
    // ===========================================================================
    void PlayOneShot(uint32_t eventHash) override;
    void PlayOneShot(uint32_t eventHash, float volume, float pitch) override;
    void PlayOneShot3D(uint32_t eventHash, const DirectX::XMFLOAT3 &position, float volume, float pitch) override;

    // ===========================================================================
    // 継続再生と制御
    // ===========================================================================
    uint32_t PlayEvent(uint32_t eventHash) override;
    uint32_t PlayEvent3D(uint32_t eventHash, const DirectX::XMFLOAT3 &position) override;
    void     StopEvent(uint32_t playingId, bool allowFadeOut = true) override;
    void     StopAll() override;
    void     SetEventParameter(uint32_t playingId, const char *paramName, float value) override;
    void     SetEvent3DAttributes(uint32_t playingId, const DirectX::XMFLOAT3 &pos) override;

    // ===========================================================================
    // 3D リスナー・演出・エディタ支援
    // ===========================================================================
    void SetListenerPosition(const DirectX::XMFLOAT3 &pos, const DirectX::XMFLOAT3 &forward,
                             const DirectX::XMFLOAT3 &up) override;

    void                     SetGlobalSlow(float slow01) override;
    void                     SetCategoryVolume(AudioCategory category, float volume01) override;
    std::vector<std::string> GetEventNames() const override;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
