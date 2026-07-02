#pragma once
#include <SimpleMath.h>

/// @brief ダッシュのパラメータと状態をひとまとめに管理するコンポーネント
struct DashComponent {
    // --- Config (プランナー調整用) ---
    float dashSpeed = 40.0f;
    float dashDuration = 0.15f;
    float dashCooldown = 0.5f;

    // ジャスト回避の「シビアさ」を決める受付時間
    float justEvadeWindow = 0.08f; 

    // --- State (実行時の状態) ---
    bool isDashing = false;
    bool isInvincible = false;

    // 現在、ジャスト回避の受付中（超シビアな時間帯）か？
    bool isJustEvadeWindowActive = false; 

    float currentTimer = 0.0f;
    DirectX::SimpleMath::Vector3 dashDirection = { 0, 0, 0 };
};