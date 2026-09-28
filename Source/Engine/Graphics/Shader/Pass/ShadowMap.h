#pragma once
// ===========================================================================
// 概要: カスケードシャドウマップ（SHADOW_CASCADE_COUNT 枚の Texture2DArray の深度）。
//
// 責務:
//   - 深度テクスチャと、カスケードごとの DSV・全カスケードを読む SRV を作る
//   - カスケード1枚を描画先にする（ShadowPass）
//   - シャドウマップ・比較サンプラー・CbShadow を、描画するシェーダーにバインドする
//   - 影の色・濃さ・バイアス・カスケードの分割距離（params）を持つ
//
// 備考: Graphics が1つ作り、RenderServices::shadowMap で渡す。
// ===========================================================================
#include <d3d11.h>
#include <DirectXMath.h>
#include "Engine/Graphics/Shader/Shared/ShaderShared.h"
#include <vector>
#include <wrl/client.h>

class ShadowMap
{
public:
    // width × height（px）の深度テクスチャをカスケードの数だけ作る
    ShadowMap(ID3D11Device *device, UINT width = 2048, UINT height = 2048);

    // cascadeIndex 番目をクリアし、描画先とビューポートにする
    void Activate(ID3D11DeviceContext *dc, int cascadeIndex);
    // 描画先を外す
    void Deactivate(ID3D11DeviceContext *dc);

    ID3D11ShaderResourceView *GetShaderResourceView() const { return shaderResourceView.Get(); }
    ID3D11SamplerState       *GetSamplerState() const { return samplerState.Get(); }

    UINT GetWidth() const { return width; }   // px
    UINT GetHeight() const { return height; } // px

    // CbShadow を書き込む
    void UpdateConstantBuffer(ID3D11DeviceContext *dc, const GpuData::CbShadow &data);

    // 影の設定（ShadowMapRenderSystem がコンポーネントから写し、ShadowPass が CbShadow に入れる）
    struct Parameters
    {
        float             shadowAttenuation                   = 0.5f;
        DirectX::XMFLOAT4 shadowColor                         = {0.0f, 0.0f, 0.0f, 0.5f};
        float             shadowBias                          = 0.0015f;                // 深度の比較に足すずれ（シャドウアクネを防ぐ）
        float             cascadeSplits[SHADOW_CASCADE_COUNT] = {15.0f, 50.0f, 200.0f}; // カスケードの境目のカメラからの距離（m）
    } params;

    // それぞれのスロットにバインドする。-1 のものはバインドしない（ShadowPass は SRV とサンプラーを -1 にする）
    void Bind(ID3D11DeviceContext *dc, int srvSlot, int samplerSlot, int cbSlot);

private:
    Microsoft::WRL::ComPtr<ID3D11Buffer> constantBuffer; // CbShadow（DYNAMIC）

    std::vector<Microsoft::WRL::ComPtr<ID3D11DepthStencilView>> depthStencilViews;  // カスケードごとの DSV
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>            shaderResourceView; // 全カスケードを読む SRV
    Microsoft::WRL::ComPtr<ID3D11SamplerState>                  samplerState;       // 比較サンプラー（LESS_EQUAL）

    D3D11_VIEWPORT viewport;

    UINT width, height; // px
};