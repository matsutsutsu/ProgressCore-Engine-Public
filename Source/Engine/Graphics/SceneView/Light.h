#pragma once
// ===========================================================================
// 概要: 1フレーム分のライトを集め、ライト定数バッファ（CbLight, b9）として GPU へ送る
//
// 責務:
//   - 平行光源1つ・点光源・スポットライトを受け取る（数は MAX_POINT_LIGHTS / MAX_SPOT_LIGHTS まで）
//   - 集めたライトを CbLight に詰めて GPU へ送り、ピクセルシェーダーにバインドする
//
// 備考: LightSystem が毎フレーム Clear してから積み直し、Graphics::Update が Bind する
//       構造体は C++ と HLSL で共有する ShaderShared.h（GpuData）の定義を使う
// ===========================================================================
#include <d3d11.h>
#include <DirectXMath.h>
#include <vector>
#include <wrl.h>
#include <wrl/client.h>

#include "Engine/Graphics/Shader/Shared/ShaderShared.h"

using DirectionalLight = GpuData::DirectionalLight;
using PointLights      = GpuData::PointLight;
using SpotLights       = GpuData::SpotLight;

class LightManager
{
public:
    using LightConstantBuffer = GpuData::CbLight;

    LightManager() = default;

    // 定数バッファを作る
    void Initialize(ID3D11Device *device);

    // 集めたライトを定数バッファへ送り、slot 番にバインドする
    void Bind(ID3D11DeviceContext *dc, int slot);

    // 点光源とスポットライトを空にする（平行光源は上書きなので残す）
    void Clear()
    {
        pointLights.clear();
        spotLights.clear();
    }

    void                    SetDirectionalLight(const DirectionalLight &light) { cbData.gDirLight = light; }
    const DirectionalLight &GetDirectionalLight() const { return cbData.gDirLight; }
    DirectionalLight       &GetDirectionalLight() { return cbData.gDirLight; }

    // 上限（MAX_POINT_LIGHTS）を超えた分は捨てる
    void AddPointLight(const PointLights &light)
    {
        if (pointLights.size() < MAX_POINT_LIGHTS) pointLights.push_back(light);
    }

    // 上限（MAX_SPOT_LIGHTS）を超えた分は捨てる
    void AddSpotLight(const SpotLights &light)
    {
        if (spotLights.size() < MAX_SPOT_LIGHTS) spotLights.push_back(light);
    }

private:
    LightConstantBuffer cbData; // GPU へ送る内容（平行光源はここに直接持つ）

    std::vector<PointLights> pointLights; // 今フレームの点光源
    std::vector<SpotLights>  spotLights;  // 今フレームのスポットライト

    Microsoft::WRL::ComPtr<ID3D11Buffer> constantBuffer; // CbLight（DYNAMIC）
};
