#pragma once
#include <d3d11.h>
#include <DirectXMath.h>
#include <vector>
#include <wrl.h>
#include <wrl/client.h>

// ==========================================================================
// ライト構造体の定義は ShaderShared.h に移動（C++ / HLSL 共有）
// ここでは型エイリアスで後方互換を確保する
// ==========================================================================
#include "Engine/Graphics/Shader/ShaderShared.h"

// --- 後方互換エイリアス ---
// 旧 struct DirectionalLight → GpuData::DirectionalLight
// 注意: フィールド名変更あり → skyColor→gSkyColor, groundColor→gGroundColor
using DirectionalLight = GpuData::DirectionalLight;

// 旧 struct PointLights → GpuData::PointLight （単数形に統一）
using PointLights = GpuData::PointLight;

// 旧 struct SpotLights → GpuData::SpotLight （単数形に統一）
using SpotLights = GpuData::SpotLight;


class LightManager
{
public:
    // -------------------------------------------------------------------------
    // MAX_POINT_LIGHTS / MAX_SPOT_LIGHTS は ShaderShared.h の #define を使用
    //   static constexpr int MAX_POINT_LIGHTS = 8; は削除
    //   → #define と同名の constexpr は構文エラーになるため
    // -------------------------------------------------------------------------

    // LightConstantBuffer は GpuData::CbLight と完全一致（フィールド名も同じ）
    // LightManager.cpp の移行先フィールド名:
    //   旧 cbData.dirLight        → cbData.gDirLight
    //   旧 cbData.pointLights[i]  → cbData.gPointLights[i]
    //   旧 cbData.spotLights[i]   → cbData.gSpotLights[i]
    //   旧 cbData.pointLightCount → cbData.gPointLightCount
    //   旧 cbData.spotLightCount  → cbData.gSpotLightCount
    using LightConstantBuffer = GpuData::CbLight;

    LightManager() = default;

    // 初期化：定数バッファの作成
    void Initialize(ID3D11Device* device);

    // CPU上のデータをGPUの定数バッファに転送し、パイプラインにバインドする
    void Bind(ID3D11DeviceContext* dc, int slot);

    // GUI描画
    void DrawGUI();

    // 全クリア
    void Clear() { pointLights.clear(); spotLights.clear(); }

    // ディレクショナルライト設定・取得
    void SetDirectionalLight(const DirectionalLight& light) { cbData.gDirLight = light; }
    const DirectionalLight& GetDirectionalLight() const { return cbData.gDirLight; }
    DirectionalLight& GetDirectionalLight() { return cbData.gDirLight; }

    // 点光源追加
    void AddPointLight(const PointLights& light) {
        if (pointLights.size() < MAX_POINT_LIGHTS) pointLights.push_back(light);
    }

    // スポットライト追加
    void AddSpotLight(const SpotLights& light) {
        if (spotLights.size() < MAX_SPOT_LIGHTS) spotLights.push_back(light);
    }

    static const char* GetBufferName() { return "CbLight"; }

    const DirectX::XMFLOAT4X4& GetLightViewProjection() const { return m_lightViewProjection; }

private:
    LightConstantBuffer cbData;

    std::vector<PointLights> pointLights;
    std::vector<SpotLights>  spotLights;

    Microsoft::WRL::ComPtr<ID3D11Buffer> constantBuffer;

    DirectX::XMFLOAT4X4 m_lightViewProjection = {
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, 0,
        0, 0, 0, 1
    };
};