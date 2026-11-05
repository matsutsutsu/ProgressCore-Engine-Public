#pragma once
// モデルファイルを使わずに、箱・球などの単純な形を描くためのコンポーネント
// 描画は PrimitiveRenderSystem。寸法はどの形でも全部持ち、形ごとに使うものだけを読む
#include <DirectXMath.h>

enum class PrimitiveType
{
    Box,
    Sphere,
    Cylinder,
    Capsule,
    Cone
};

struct PrimitiveComponent
{
    PrimitiveType type = PrimitiveType::Box;

    DirectX::XMFLOAT3 size   = {1.0f, 1.0f, 1.0f}; // Box の各軸の半分の長さ（m）。箱メッシュが ±1 なので辺の長さは2倍になる
    float             radius = 0.5f;               // Sphere・Cylinder・Capsule・Cone の半径（m）
    float             height = 1.0f;               // Cylinder・Capsule・Cone の高さ（m）

    DirectX::XMFLOAT4 color = {1.0f, 1.0f, 1.0f, 1.0f}; // 描画色（RGBA）

    bool isWireframe = false; // ワイヤーフレームで描くか
};
