#pragma once
#include <DirectXMath.h>

struct ShadowMapConfigComponent {
    float shadowBias = 0.0015f;
    DirectX::XMFLOAT4 shadowColor = { 0.0f, 0.0f, 0.0f, 0.5f }; // 影の色（濃さ）
    float orthoSize = 35.0f; // 影を落とす範囲
};