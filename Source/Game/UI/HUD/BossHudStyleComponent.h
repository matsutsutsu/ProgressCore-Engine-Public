#pragma once

// ボスの HP ゲージの振る舞いの調整値。BossHud プレハブのルートに付ける
struct BossHudStyleComponent
{
    // --- 被弾 ---
    float trailHold  = 0.35f; // 遅れて減るバーが動き出すまでの時間（秒）
    float trailSpeed = 5.0f;  // 遅れて減るバーが追いつく速さ（大きいほど速い）
    float shakeDecay = 9.0f;  // 揺れが収まる速さ（大きいほど短い）

    // --- 第2形態の外側の光 ---
    float phase2AuraMin   = 0.2f;  // 明滅の暗いときの強さ（0〜1）
    float phase2AuraMax   = 1.0f;  // 明滅の明るいときの強さ（0〜1）
    float phase2AuraPulse = 0.32f; // 明滅の速さ（回/秒）
};
