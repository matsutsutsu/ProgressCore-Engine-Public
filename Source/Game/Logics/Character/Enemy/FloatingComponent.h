#pragma once
#include <DirectXMath.h>

struct FloatingComponent {
    // 基準位置
    // Y: 固定したい高さ
    // X, Z: 今回のシンプル実装では無視されます（移動の邪魔をしないため）
    DirectX::XMFLOAT3 center = {0.0f, 2.0f, 0.0f};

    // 振れ幅
    // Y成分だけを設定すれば上下に揺れます (例: 0.0, 0.5, 0.0)
    DirectX::XMFLOAT3 amplitude = {0.0f, 0.5f, 0.0f};

    // 揺れる速度
    DirectX::XMFLOAT3 speed = {1.0f, 2.0f, 1.0f};

    // 個体差
    float timeOffset = 0.0f;
};