#pragma once
#include "ECS/Common/CCL_Common.h"
#include <DirectXMath.h>

struct PlayerCarComponent {
    // --- 抽象化された入力データ (Input State) ---
    struct InputData {
        float throttle = 0.0f;
        float steering = 0.0f;
        bool jump = false;
    } input;

    CCL::ECS::EntityID physicsSphereID = CCL::ECS::InvalidEntityID;

    float acceleration = 2000.0f;
    float turnSpeed = 150.0f;

    // =======================================================
    // 描画モデルのズレ補正 (Visual Offset & Rotation)
    // =======================================================
    DirectX::XMFLOAT3 visualOffset = { 0.0f, -1.0f, 0.0f };  // 位置のズレ補正（ローカル空間）
    DirectX::XMFLOAT3 visualRotation = { 0.0f, 0.0f, 0.0f }; // ★追加: 回転のズレ補正（オイラー角/度数法）

    // --- 内部状態（インスペクタには出さない） ---
    DirectX::XMFLOAT4 logicRotation = { 0.0f, 0.0f, 0.0f, 1.0f }; // 車の「論理的」な向き（進行方向）
    bool isLogicRotInitialized = false; // 初期化フラグ
};