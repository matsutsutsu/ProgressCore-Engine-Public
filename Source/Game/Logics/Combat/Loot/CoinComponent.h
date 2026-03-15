#pragma once
#include "ECS/Common/CCL_Common.h"

struct CoinComponent {
    int   amount   = 10;
    float lifeTime = 15.0f;

    // --- 吸い寄せ設定 ---
    float magnetRange  = 5.0f;
    float magnetPower  = 40.0f; // 吸い寄せる力の強さ
    bool  isMagnetized = false;

    // --- バウンド設定 ---
    float bounce   = 0.6f;  // 跳ね返り係数
    float friction = 0.95f; // 横滑りの摩擦

    // --- ★追加: アニメーション用 ---
    float animTimer     = 0.0f;   // 浮遊アニメーションの時間
    float rotationSpeed = 180.0f; // 回転速度 (度/秒)
    float floatSpeed    = 2.0f;   // 浮遊速度
    float floatRange    = 0.2f;   // 浮遊幅
    float baseY         = 0.0f;   // 浮遊の基準Y座標 (接地時に設定)
};