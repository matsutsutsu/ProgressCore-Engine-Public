#pragma once
#include <DirectXMath.h>

// ============================================================================
// 平行光源コンポーネント
// ============================================================================
// CustomGui タグ: CustomGui 関数は LightComponentMeta.cpp の手書き部分が担当
// ジェネレータは REGISTER_COMPONENT のみを生成する

struct DirectionalLightComponent {

        DirectX::XMFLOAT3 color = { 1.0f, 1.0f, 1.0f };


        float intensity = 0.2f;

        DirectX::XMFLOAT4 skyColor = { 0.3f, 0.4f, 0.6f, 1.0f };


        DirectX::XMFLOAT4 groundColor = { 0.1f, 0.1f, 0.05f, 1.0f };

    // 向きは TransformComponent の Rotation から計算する (シリアライズ不要)
};

// ============================================================================
// 点光源コンポーネント
// ============================================================================

struct PointLightComponent {

        DirectX::XMFLOAT3 color = { 1.0f, 1.0f, 1.0f };

        float intensity = 1.0f;

        float range = 10.0f;

    // 減衰パラメータ
        float constant = 1.0f;

        float linear = 0.1f;

        float quadratic = 0.01f;

    // 位置は TransformComponent.position を使う
};

// ============================================================================
// スポットライトコンポーネント
// ============================================================================
struct SpotLightComponent {
        DirectX::XMFLOAT3 color = { 1.0f, 1.0f, 1.0f };

        float intensity = 1.0f;

        float range = 10.0f;

        float innerCos = 0.9f;

        float outerCos = 0.8f;

    // 位置・向きは TransformComponent を使う
};