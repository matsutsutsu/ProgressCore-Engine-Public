#pragma once
#include <DirectXMath.h>

struct JoltBoxColliderComponent {
    // Joltの仕様に合わせ、最初から「半分のサイズ(Half-Extent)」で定義する
    DirectX::XMFLOAT3 halfExtent = {0.5f, 0.5f, 0.5f};
    bool isDirty = false; // サイズを変更した時にtrueにする
};