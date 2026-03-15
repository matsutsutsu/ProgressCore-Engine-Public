#pragma once
#include <d3d11.h>
#include <wrl/client.h>
#include <DirectXMath.h>


// ShadowMap.h
class ShadowMap {
public:
    ShadowMap(ID3D11Device* device, UINT width = 2048, UINT height = 2048);

    // 切り替え関数
    void Activate(ID3D11DeviceContext* dc);	// シャドウマップに切り替え
    //void Activate(ID3D11DeviceContext* dc, int cascadeIndex); // カスケードシャドウマップ用
    void Deactivate(ID3D11DeviceContext* dc); // 元に戻す

    // ゲッター
    ID3D11ShaderResourceView* GetShaderResourceView() const { return shaderResourceView.Get(); }
    ID3D11SamplerState* GetSamplerState() const { return samplerState.Get(); }

    UINT GetWidth() const { return width; }
    UINT GetHeight() const { return height; }

    void DrawGUI();

	// 定数バッファ構造体
    struct CbShadow {
        DirectX::XMFLOAT4X4 lightViewProjection;
        DirectX::XMFLOAT4 shadowColor;
        float shadowAttenuation;
        float shadowBias;
        DirectX::XMFLOAT2 shadowDummy;
    };


    // 定数バッファのデータを更新するための関数
    void UpdateConstantBuffer(ID3D11DeviceContext* dc, const CbShadow& data);


    // ImGuiで操作したいパラメータを構造体にまとめる
    struct Parameters {
        float shadowAttenuation = 0.5f;
        DirectX::XMFLOAT4 shadowColor = { 0.0f, 0.0f, 0.0f, 0.5f };
        float shadowBias = 0.0015f; // 初期値は小さめに
        float orthoSize = 35.0f;
    } params;

    // 現在のパラメータから定数バッファ用データを生成するヘルパー
    CbShadow CreateCbData(const DirectX::XMFLOAT4X4& lightVP) const {
        CbShadow cb{};
        cb.lightViewProjection = lightVP;
        cb.shadowColor         = params.shadowColor;
        cb.shadowAttenuation   = params.shadowAttenuation;
        cb.shadowBias          = params.shadowBias;
        return cb;
    }

    // デフォルト引数を削除し、リフレクションから取得したスロットを渡す形にする
    void Bind(ID3D11DeviceContext* dc, int srvSlot, int samplerSlot, int cbSlot);

    // リフレクションで検索するための標準的な名前を定義
    static const char* GetSrvName() { return "ShadowMap"; }
    static const char* GetSamplerName() { return "ShadowSampler"; }
    static const char* GetCbName() { return "CbShadow"; }
private:
    Microsoft::WRL::ComPtr<ID3D11Buffer> constantBuffer;

    Microsoft::WRL::ComPtr<ID3D11DepthStencilView> depthStencilView; // カスケード用
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> shaderResourceView;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> samplerState;

    D3D11_VIEWPORT viewport;

    Microsoft::WRL::ComPtr<ID3D11Texture2D> depthBuffer;

    // 元の状態保存用
    D3D11_VIEWPORT cachedViewport;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> cachedRTV;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilView> cachedDSV;

    UINT width, height;
};