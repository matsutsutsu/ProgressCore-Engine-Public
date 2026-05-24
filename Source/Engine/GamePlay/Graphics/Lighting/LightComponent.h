#pragma once
#include <DirectXMath.h>

#include "Engine/Serialization/Reflection/Reflection.h"

// ============================================================================
// 平行光源コンポーネント
// ============================================================================
// CustomGui タグ: CustomGui 関数は LightComponentMeta.cpp の手書き部分が担当
// ジェネレータは REGISTER_COMPONENT のみを生成する
CCL_COMPONENT("DirectionalLight", "CustomGui")
struct DirectionalLightComponent {
    CCL_FIELD("Color", "Lighting")
        DirectX::XMFLOAT3 color = { 1.0f, 1.0f, 1.0f };

    CCL_FIELD_FLOAT_RANGED("Intensity", 0.05f, 0.0f, 100.0f, "Lighting")
        float intensity = 0.2f;

    CCL_FIELD("Sky Color", "Hemisphere")
        DirectX::XMFLOAT4 skyColor = { 0.3f, 0.4f, 0.6f, 1.0f };

    CCL_FIELD("Ground Color", "Hemisphere")
        DirectX::XMFLOAT4 groundColor = { 0.1f, 0.1f, 0.05f, 1.0f };

    // 向きは TransformComponent の Rotation から計算する (シリアライズ不要)
};

// ============================================================================
// 点光源コンポーネント
// ============================================================================
CCL_COMPONENT("PointLight", "CustomGui")
struct PointLightComponent {
    CCL_FIELD("Color", "Lighting")
        DirectX::XMFLOAT3 color = { 1.0f, 1.0f, 1.0f };

    CCL_FIELD_FLOAT_RANGED("Intensity", 0.05f, 0.0f, 100.0f, "Lighting")
        float intensity = 1.0f;

    CCL_FIELD_FLOAT_RANGED("Range", 0.5f, 0.0f, 1000.0f, "Lighting")
        float range = 10.0f;

    // 減衰パラメータ
    CCL_FIELD_FLOAT_RANGED("Constant", 0.01f, 0.0f, 10.0f, "Attenuation")
        float constant = 1.0f;

    CCL_FIELD_FLOAT_RANGED("Linear", 0.01f, 0.0f, 10.0f, "Attenuation")
        float linear = 0.1f;

    CCL_FIELD_FLOAT_RANGED("Quadratic", 0.001f, 0.0f, 1.0f, "Attenuation")
        float quadratic = 0.01f;

    // 位置は TransformComponent.position を使う
};

// ============================================================================
// スポットライトコンポーネント
// ============================================================================
CCL_COMPONENT("SpotLight", "CustomGui")
struct SpotLightComponent {
    CCL_FIELD("Color", "Lighting")
        DirectX::XMFLOAT3 color = { 1.0f, 1.0f, 1.0f };

    CCL_FIELD_FLOAT_RANGED("Intensity", 0.05f, 0.0f, 100.0f, "Lighting")
        float intensity = 1.0f;

    CCL_FIELD_FLOAT_RANGED("Range", 0.5f, 0.0f, 1000.0f, "Lighting")
        float range = 10.0f;

    CCL_FIELD_FLOAT_RANGED("Inner Cone (Cos)", 0.01f, 0.0f, 1.0f, "Cone")
        float innerCos = 0.9f;

    CCL_FIELD_FLOAT_RANGED("Outer Cone (Cos)", 0.01f, 0.0f, 1.0f, "Cone")
        float outerCos = 0.8f;

    // 位置・向きは TransformComponent を使う
};