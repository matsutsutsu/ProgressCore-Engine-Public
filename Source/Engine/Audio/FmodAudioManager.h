#pragma once
#include "IAudioAPI.h"
#include <fmod_studio.hpp>
#include <unordered_map>

class FmodAudioManager : public IAudioAPI {
private:
    FMOD::Studio::System* m_studioSystem = nullptr;

    // FMODのBankデータを保持するリスト
    std::vector<FMOD::Studio::Bank*> m_loadedBanks;

    // ハッシュ値 -> 音の設計図(EventDescription) の最速検索辞書
    std::unordered_map<uint32_t, FMOD::Studio::EventDescription*> m_eventCache;

    // ヘルパー：DirectXのベクトルをFMODのベクトルに変換
    inline FMOD_VECTOR ToFMOD(const DirectX::XMFLOAT3& v) const {
        return { v.x, v.y, v.z };
    }

public:
    FmodAudioManager() = default;
    virtual ~FmodAudioManager();

    void Initialize() override;
    void Update() override;

    void LoadBank(const char* bankFilePath) override;
    void LoadEvent(const char* eventPath) override;

    void PlayEvent(uint32_t eventHash) override;
    void PlayEvent3D(uint32_t eventHash, const DirectX::XMFLOAT3& position) override;

    void SetListenerPosition(const DirectX::XMFLOAT3& pos, const DirectX::XMFLOAT3& forward, const DirectX::XMFLOAT3& up) override;
};