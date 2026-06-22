#pragma once
#include <SimpleMath.h>

/// @brief ダッシュのパラメータと状態をひとまとめに管理するコンポーネント
struct DashComponent {
    // --- Config (プランナー調整用) ---
    float dashSpeed = 40.0f;
    float dashDuration = 0.15f;
    float dashCooldown = 0.5f;

    // --- State (実行時の状態) ---
    bool isDashing = false;
    bool isInvincible = false;
    float currentTimer = 0.0f;
    DirectX::SimpleMath::Vector3 dashDirection = { 0, 0, 0 };
};