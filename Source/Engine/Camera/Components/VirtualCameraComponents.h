#pragma once
#include "Engine/Serialization/Core/EntityLink.h" // ★追加: EntityLinkを使用するため
#include <DirectXMath.h>
#include <algorithm> // for max

// 1. 仮想カメラの基本情報 (vCam)
struct VirtualCamera {
    int priority = 0;
    float blendTime = 1.0f;
    float fov = 45.0f;
    float nearClip = 0.1f;
    float farClip = 1000.0f;
    DirectX::XMFLOAT3 resultPos = { 0, 0, 0 };
    DirectX::XMFLOAT3 resultLookAt = { 0, 0, 1 };
    DirectX::XMFLOAT3 resultUp = { 0, 1, 0 };
    bool isValid = false;
};

// 2. 「移動」戦略: ターゲット追従 (Body)
struct CameraBodyFollow {
    // ★ 修正: 生のIDではなく安全なEntityLinkを使用する
    CCL::Serialization::EntityLink target;
    DirectX::XMFLOAT3  offset = { 0, 0, 0 };
    float              damping = 10.0f;
    bool               lockY = false;
};

// 3. 「回転」戦略: ターゲット注視 (Aim)
struct CameraAimLookAt {
    // ★ 修正: EntityLinkを使用する
    CCL::Serialization::EntityLink target;
    DirectX::XMFLOAT3  offset = { 0, 0, 0 };
    float              damping = 10.0f;
};

// 4. 「揺れ」演出用コンポーネント (Shake)
struct CameraShake {
    float amplitude = 0.0f;
    float duration = 0.0f;
    float maxDuration = 1.0f;

    void SetShake(float strength, float time)
    {
        amplitude = strength;
        maxDuration = time;
        duration = time;
    }
};

// ---------------------------------------------------------
// 新しい拡張: 自由移動用ボディコンポーネント
// ---------------------------------------------------------
struct CameraBodyFree {
    float moveSpeed = 10.0f;
    float lookSpeed = 0.2f;
    float currentYaw = 0.0f;
    float currentPitch = 0.0f;
};

