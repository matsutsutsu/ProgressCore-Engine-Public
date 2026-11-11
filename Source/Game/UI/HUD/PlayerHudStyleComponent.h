#pragma once
#include <DirectXMath.h>

// プレイヤーの HP ゲージの振る舞いの調整値。PlayerHud プレハブのルートに付ける
struct PlayerHudStyleComponent
{
    // --- 色 ---
    DirectX::XMFLOAT4 fillNormal = {0.96f, 0.88f, 0.62f, 1.0f}; // 普段の色。鎧の黄に合わせた白金
    DirectX::XMFLOAT4 fillLow    = {0.92f, 0.24f, 0.20f, 1.0f}; // 瀕死の色

    // --- 瀕死の演出 ---
    float lowHpStart = 0.40f; // この割合を下回ると赤へ寄せ始める
    float lowHpFull  = 0.25f; // この割合以下で完全に赤にする
    float pulseHz    = 1.6f;  // 脈打ちの速さ（回/秒）
    float pulseDepth = 0.35f; // 脈打ちで暗くする深さ（0 なら脈打たない）

    // --- 被弾 ---
    float trailHold  = 0.30f; // 遅れて減るバーが動き出すまでの時間（秒）
    float trailSpeed = 6.0f;  // 遅れて減るバーが追いつく速さ（大きいほど速い）
    float shakeDecay = 9.0f;  // 揺れが収まる速さ（大きいほど短い）
};
