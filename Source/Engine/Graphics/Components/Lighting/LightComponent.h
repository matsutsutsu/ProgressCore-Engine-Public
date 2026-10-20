#pragma once
// ===========================================================================
// 概要: 平行光源・点光源・スポットライトのコンポーネント。
//
// 責務:
//   - 各ライトの色・強さ・範囲・減衰などのパラメータを持つ
//
// 備考: 位置と向きは TransformComponent から取るので、ここには持たない。
//       LightSystem が毎フレーム LightManager へ積み、Graphics::Update が GPU へ送る。
// ===========================================================================
#include <DirectXMath.h>

// 平行光源。向きは TransformComponent の回転から計算する
struct DirectionalLightComponent
{
    DirectX::XMFLOAT3 color     = {1.0f, 1.0f, 1.0f}; // 光の色
    float             intensity = 0.2f;               // 光の強さ（color に掛ける倍率）

    DirectX::XMFLOAT4 skyColor    = {0.3f, 0.4f, 0.6f, 1.0f};   // 半球ライティングの上方向（空）の色
    DirectX::XMFLOAT4 groundColor = {0.1f, 0.1f, 0.05f, 1.0f};  // 半球ライティングの下方向（地面）の色
};

// 点光源。位置は TransformComponent のワールド位置を使う
struct PointLightComponent
{
    DirectX::XMFLOAT3 color     = {1.0f, 1.0f, 1.0f}; // 光の色
    float             intensity = 1.0f;               // 光の強さ（color に掛ける倍率）
    float             range     = 10.0f;              // 光が届く距離（m）

    // 距離減衰 1 / (constant + linear * d + quadratic * d^2) の係数
    float constant  = 1.0f;
    float linear    = 0.1f;
    float quadratic = 0.01f;
};

// スポットライト。位置と向きは TransformComponent を使う
struct SpotLightComponent
{
    DirectX::XMFLOAT3 color     = {1.0f, 1.0f, 1.0f}; // 光の色
    float             intensity = 1.0f;               // 光の強さ（color に掛ける倍率）
    float             range     = 10.0f;              // 光が届く距離（m）

    float innerCos = 0.9f; // 内側コーンの半角の cos。これより内側は最大の明るさ
    float outerCos = 0.8f; // 外側コーンの半角の cos。これより外側は光らない（innerCos 以下にする）

    // 距離減衰 1 / (constant + linear * d + quadratic * d^2) の係数
    float constant  = 1.0f;
    float linear    = 0.1f;
    float quadratic = 0.01f;
};
