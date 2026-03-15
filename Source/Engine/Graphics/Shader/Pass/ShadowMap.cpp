// ShadowMap.cpp
#include "ShadowMap.h"
#include <imgui.h>
#include "Engine/Core/Common/Misc.h"
#include "Engine/Graphics/Core/GpuResourceUtils.h"


// コンストラクタ
ShadowMap::ShadowMap(ID3D11Device* device, UINT width, UINT height)
{
    HRESULT hr = S_OK;

    this->width = width;
    this->height = height;

    // テクスチャの器を作成
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R24G8_TYPELESS;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
    desc.CPUAccessFlags = 0;
    desc.MiscFlags = 0;

    {
        hr = device->CreateTexture2D(&desc, nullptr, depthBuffer.GetAddressOf());
        _ASSERT_EXPR(SUCCEEDED(hr), HRTrace(hr));
    }

    // 書き込む用の口を作る
    D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
    {
        dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
        dsvDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
        dsvDesc.Texture2D.MipSlice = 0;

        hr = device->CreateDepthStencilView(depthBuffer.Get(), &dsvDesc,
            depthStencilView.GetAddressOf());
        _ASSERT_EXPR(SUCCEEDED(hr), HRTrace(hr));
    }

	// 読み込む用の口を作る
    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    {
        srvDesc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MostDetailedMip = 0;
        srvDesc.Texture2D.MipLevels = 1;

        hr = device->CreateShaderResourceView(depthBuffer.Get(), &srvDesc,
            shaderResourceView.GetAddressOf());
        _ASSERT_EXPR(SUCCEEDED(hr), HRTrace(hr));
    }

    // サンプラーステート作成
    D3D11_SAMPLER_DESC samplerDesc{};
    // ここが重要：COMPARISON（比較）付きのフィルタを指定する
    samplerDesc.Filter = D3D11_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR;
    samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_BORDER; // 影の外側対策
    samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_BORDER;
    samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_BORDER;
    samplerDesc.BorderColor[0] = 1.0f; // 影の外を「白（影なし）」にする
    samplerDesc.BorderColor[1] = 1.0f;
    samplerDesc.BorderColor[2] = 1.0f;
    samplerDesc.BorderColor[3] = 1.0f;
    // ここが重要：深度比較の関数を指定する
    samplerDesc.ComparisonFunc = D3D11_COMPARISON_LESS_EQUAL;
    samplerDesc.MinLOD = 0;
    samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;

    hr = device->CreateSamplerState(&samplerDesc, samplerState.GetAddressOf());
    _ASSERT_EXPR(SUCCEEDED(hr), HRTrace(hr));

    // ビューポート設定
    viewport.Width = static_cast<float>(width);
    viewport.Height = static_cast<float>(height);
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    viewport.TopLeftX = 0.0f;
    viewport.TopLeftY = 0.0f;

    // width/heightを保存
    this->width = width;
    this->height = height;

    // ShadowMap::ShadowMap コンストラクタ内に追加
    HRESULT hrCB = GpuResourceUtils::CreateDynamicConstantBuffer(
        device,
        sizeof(CbShadow),
        constantBuffer.GetAddressOf()
    );
    _ASSERT_EXPR(SUCCEEDED(hrCB), L"Failed to create ShadowMap Constant Buffer");
}


// シャドウマップに切り替え
void ShadowMap::Activate(ID3D11DeviceContext* dc)
{
	// PSシェーダーのスロット10にバインドされているSRVを解除
    // これから書き込むテクスチャが別のシェーダーで使われているとエラーになるから
	// nullをセットして解除する
    ID3D11ShaderResourceView* nullSRV[] = { nullptr };
    dc->PSSetShaderResources(10, 1, nullSRV);

    // 現在の状態を保存
    UINT numViewports = 1;
	// 今の画面サイズ（Viewport）と本来の描画先（RenderTarget）を後で戻れるように保存しておく
    dc->RSGetViewports(&numViewports, &cachedViewport);
    dc->OMGetRenderTargets(1, cachedRTV.ReleaseAndGetAddressOf(),
        cachedDSV.ReleaseAndGetAddressOf());

    // シャドウマップ用に切り替え
	// 影のデータを書き込むために、深度ステンシルビューをクリアしてセットする
    dc->ClearDepthStencilView(depthStencilView.Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);
    // 画面サイズをシャドウマップの解像度（例えば2048px）に合わせる
	// これをしないとシャドウマップが正しく描画されない
    dc->RSSetViewports(1, &viewport);
	// 描画先を深度ステンシルビューのみに設定（レンダーターゲットはなし）
	// 第1引数を0にして、第2引数をnullptrにすることで
	// 「色は塗らない(RenderTargetなし)、深度情報(Depth)だけを書き込む」ように設定している
    dc->OMSetRenderTargets(0, nullptr, depthStencilView.Get());
}

// カスケードシャドウマップ用切り替え関数
//void ShadowMap::Activate(ID3D11DeviceContext* dc, int cascadeIndex)
//{
//    ID3D11ShaderResourceView* nullSRV[] = { nullptr };
//    //dc->PSSetShaderResources(5, 1, nullSRV);
//
//    // 現在の状態を保存
//    UINT numViewports = 1;
//    dc->RSGetViewports(&numViewports, &cachedViewport);
//    dc->OMGetRenderTargets(1, cachedRTV.ReleaseAndGetAddressOf(),
//        cachedDSV.ReleaseAndGetAddressOf());
//
//    // シャドウマップ用に切り替え
//    dc->ClearDepthStencilView(depthStencilView.Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);
//    dc->RSSetViewports(1, &viewport);
//    dc->OMSetRenderTargets(0, nullptr, depthStencilView.Get());
//}

// 元に戻す
void ShadowMap::Deactivate(ID3D11DeviceContext* dc)
{
    // 保存した状態に戻す
	// Activeで保存したViewportとRenderTargetを復元する
    dc->RSSetViewports(1, &cachedViewport);
    dc->OMSetRenderTargets(1, cachedRTV.GetAddressOf(), cachedDSV.Get());
}


void ShadowMap::DrawGUI() {
    if (ImGui::CollapsingHeader(u8"ShadowMap 設定 (Control)")) {

        // 影の浮き・縞模様の調整
        ImGui::DragFloat(u8"影のバイアス (Bias)", &params.shadowBias, 0.0001f, 0.0f, 0.05f, "%.4f");
        ImGui::Separator();

        // 影の濃さ・境界の減衰
        ImGui::DragFloat(u8"影の減衰 (Attenuation)", &params.shadowAttenuation, 0.01f, 0.0f, 2.0f);
        ImGui::ColorEdit4(u8"影の色 (Shadow Color)", &params.shadowColor.x);
        ImGui::Separator();

        // ライトが照らす範囲
        ImGui::DragFloat(u8"ライトの投影範囲 (Ortho Size)", &params.orthoSize, 0.1f, 1.0f, 100.0f);

        if (shaderResourceView) {
            ImGui::Text(u8"シャドウマップ・バッファの確認:");
            // 深度値が見えやすいようにプレビュー表示
            ImGui::Image((void*)shaderResourceView.Get(), ImVec2(256, 256),
                ImVec2(0, 0), ImVec2(1, 1), ImVec4(1, 1, 1, 1), ImVec4(1, 1, 1, 1));
        }
    }
}

void ShadowMap::Bind(ID3D11DeviceContext* dc, int srvSlot, int samplerSlot, int cbSlot)
{
    // 1. テクスチャのバインド
    if (srvSlot >= 0) {
        ID3D11ShaderResourceView* srvs[] = { shaderResourceView.Get() };
        dc->PSSetShaderResources((UINT)srvSlot, 1, srvs);
    }

    // 2. サンプラーのバインド
    if (samplerSlot >= 0) {
        ID3D11SamplerState* samplers[] = { samplerState.Get() };
        dc->PSSetSamplers((UINT)samplerSlot, 1, samplers);
    }

    // 3. 定数バッファのバインド
    if (constantBuffer && cbSlot >= 0) {
        ID3D11Buffer* cbs[] = { constantBuffer.Get() };
        // 座標変換に使うためVS、影の濃さなどの判定に使うためPSの両方にセット
        dc->VSSetConstantBuffers((UINT)cbSlot, 1, cbs);
        dc->PSSetConstantBuffers((UINT)cbSlot, 1, cbs);
    }
}

void ShadowMap::UpdateConstantBuffer(ID3D11DeviceContext* dc, const CbShadow& data)
{
    if (!constantBuffer) return;

    // GPUリソースをマップして書き込み可能にする
    D3D11_MAPPED_SUBRESOURCE mappedResource;
	// D3D11_MAP_WRITE_DISCARD を使って、古いデータは破棄する
    HRESULT hr = dc->Map(
        constantBuffer.Get(),
        0,
        D3D11_MAP_WRITE_DISCARD,
        0,
        &mappedResource
    );

    if (SUCCEEDED(hr))
    {
        // CPU上の構造体データをGPUバッファへコピー
        memcpy(mappedResource.pData, &data, sizeof(CbShadow));

        // アンマップしてGPU側のアクセスを再開
        dc->Unmap(constantBuffer.Get(), 0);
    }
    else
    {
        // エラーハンドリング
        OutputDebugStringA("SHADOWMAP CONSTANT BUFFER MAP FAILED\n");
    }
}
