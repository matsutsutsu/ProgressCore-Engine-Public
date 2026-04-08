#pragma once
#include <string>
#include "Engine/Core/Math/StringHash.h" // プロジェクトのパスに合わせてください
#include <DirectXMath.h>

class IAudioAPI {
public:
    virtual ~IAudioAPI() = default;

    // --- システム管理 ---
    virtual void Initialize() = 0;
    virtual void Update() = 0; // 毎フレーム呼ぶ必要がある

    // --- データロード ---
    virtual void LoadBank(const char* bankFilePath) = 0;
    virtual void LoadEvent(const char* eventPath) = 0; // 辞書にイベントを事前登録する

    // --- 再生 (O(1) ハッシュ最速版) ---
    virtual void PlayEvent(uint32_t eventHash) = 0;
    virtual void PlayEvent3D(uint32_t eventHash, const DirectX::XMFLOAT3& position) = 0;

    // --- リスナー (カメラ/プレイヤーの耳の位置) ---
    virtual void SetListenerPosition(const DirectX::XMFLOAT3& pos, const DirectX::XMFLOAT3& forward, const DirectX::XMFLOAT3& up) = 0;

    // =======================================================
    // 既存コードを壊さない＆書きやすくするためのヘルパー
    // =======================================================
    void PlayEvent(const char* eventPath) { PlayEvent(CCL::Utils::HashString(eventPath)); }
    void PlayEvent3D(const char* eventPath, const DirectX::XMFLOAT3& position) { PlayEvent3D(CCL::Utils::HashString(eventPath), position); }
};