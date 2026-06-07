#pragma once
#include <DirectXMath.h>

struct ToneMapConfigComponent {
    float exposure = 0.2f;               // 露出
    float chromaticAberration = 0.0f;    // 色収差の強さ（通常0、ヒットストップ時などに0.02〜0.05へ上げる）
    DirectX::XMFLOAT3 colorTint = { 1.0f, 1.0f, 1.0f }; // 画面全体の色味（カラーグレーディング用）
};