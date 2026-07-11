#include "PostProcessManager.h"
#include "Engine/Graphics/RHI/GpuResourceUtils.h"
#include "Engine/Graphics/Shader/ShaderResources.h"

#include <d3d11_1.h> // GPUマーカー用

// 各種エフェクトクラスのインクルード
#include "BloomEffect.h"
#include "DistortionEffect.h"
#include "RadialBlurEffect.h"
#include "VignetteEffect.h"
#include "ChromaticAberrationEffect.h"


void PostProcessManager::Initialize(ID3D11Device* device, UINT width, UINT height)
{
    _device = device;

    _toneMapEffect = std::make_unique<ToneMapEffect>();
    _toneMapEffect->Initialize(device);
  
    // ==========================================================
    // 2. エフェクトの生産ラインを構築
    // ==========================================================

    AddEffect<DistortionEffect>();
    AddEffect<RadialBlurEffect>();
    AddEffect<BloomEffect>();
    AddEffect<ChromaticAberrationEffect>();
    AddEffect<VignetteEffect>();


    Resize(device, width, height);
}

PostProcessManager::~PostProcessManager()
{
    _effects.clear(); // 各 IPostEffect が自前の RT を解放する

    if (_hdrBuffers[0].IsValid()) ResourceManager::Instance().UnloadRenderTarget(_hdrBuffers[0]);
    if (_hdrBuffers[1].IsValid()) ResourceManager::Instance().UnloadRenderTarget(_hdrBuffers[1]);
    if (_outputSDR.IsValid()) ResourceManager::Instance().UnloadRenderTarget(_outputSDR);
}

void PostProcessManager::Resize(ID3D11Device* device, UINT width, UINT height)
{
    if (width == 0 || height == 0) return;
    _width = width;
    _height = height;

    if(_hdrBuffers[0].IsValid()) ResourceManager::Instance().UnloadRenderTarget(_hdrBuffers[0]);
    if (_hdrBuffers[1].IsValid()) ResourceManager::Instance().UnloadRenderTarget(_hdrBuffers[1]);
    if (_outputSDR.IsValid()) ResourceManager::Instance().UnloadRenderTarget(_outputSDR);

    // ★ フル解像度の HDR キャンバスを2枚確保
    _hdrBuffers[0] = ResourceManager::Instance().CreateRenderTarget(width, height, DXGI_FORMAT_R16G16B16A16_FLOAT);
    _hdrBuffers[1] = ResourceManager::Instance().CreateRenderTarget(width, height, DXGI_FORMAT_R16G16B16A16_FLOAT);
    _outputSDR = ResourceManager::Instance().CreateRenderTarget(width, height, DXGI_FORMAT_R8G8B8A8_UNORM);

    // 全エフェクトの中間 RT もリサイズ
    for (auto& effect : _effects) {
        effect->Resize(device, width, height);
    }
}

void PostProcessManager::BeginScene(ID3D11DeviceContext* dc,
    const float          clearColor[4],
    ID3D11DepthStencilView* dsv)
{
    // 初期描画は A (_hdrBuffers[0]) に行う
    RenderTarget* sceneHDR = ResourceManager::Instance().GetRenderTarget(_hdrBuffers[0]);
    if (!sceneHDR) return;

    float w = static_cast<float>(sceneHDR->GetWidth());
    float h = static_cast<float>(sceneHDR->GetHeight());
    if (w <= 0.0f || w > 16384.0f || h <= 0.0f || h > 16384.0f) return;

    sceneHDR->Clear(dc, clearColor);

    ID3D11RenderTargetView* rtv = sceneHDR->GetRTV();
    dc->OMSetRenderTargets(1, &rtv, dsv);

    D3D11_VIEWPORT vp = {};
    vp.Width = w;
    vp.Height = h;
    vp.MaxDepth = 1.0f;
    dc->RSSetViewports(1, &vp);
}

ID3D11ShaderResourceView* PostProcessManager::EndScene(ID3D11DeviceContext* dc, ID3D11RenderTargetView* targetRTV)
{
    RenderTarget* outputSDR = ResourceManager::Instance().GetRenderTarget(_outputSDR);
    if (!outputSDR) return nullptr;

    // RTV 登録を解除
    ID3D11RenderTargetView* nullRTV = nullptr;
    dc->OMSetRenderTargets(1, &nullRTV, nullptr);

    // ================================================================
    // ★ 究極のエコシステム: Ping-Pong 実行
    // ================================================================
    int readIdx = 0;
    int writeIdx = 1;


    // 描画ループの直前・直後でこれを確認してください
    D3D11_VIEWPORT vpHDR = {};
    vpHDR.Width = static_cast<float>(_width);  // メンバ変数の幅
    vpHDR.Height = static_cast<float>(_height); // メンバ変数の高さ
    vpHDR.MinDepth = 0.0f;
    vpHDR.MaxDepth = 1.0f;
    dc->RSSetViewports(1, &vpHDR);


#if defined(ENABLE_EDITOR)
    // 1回だけ Query して、存在する場合のみポインタを保持
    Microsoft::WRL::ComPtr<ID3DUserDefinedAnnotation> annotation;
    dc->QueryInterface(IID_PPV_ARGS(annotation.GetAddressOf()));
#endif

    for (auto& effect : _effects) {
        if (!effect->IsEnabled()) continue;

        // ★ GPUマーカー（デバッグビルド時のみ）
#if defined(ENABLE_EDITOR)
        if (annotation) {
            std::wstring wname(effect->GetName(), effect->GetName() + strlen(effect->GetName()));
            annotation->BeginEvent(wname.c_str());
        }
#endif

        RenderTarget* readRT = ResourceManager::Instance().GetRenderTarget(_hdrBuffers[readIdx]);
        RenderTarget* writeRT = ResourceManager::Instance().GetRenderTarget(_hdrBuffers[writeIdx]);

        dc->RSSetViewports(1, &vpHDR);

        // A を読み込み B に書く
        effect->Process(dc, readRT->GetSRV(), writeRT->GetRTV());

        // ★ マーカー終了
#if defined(ENABLE_EDITOR)
        if (annotation) annotation->EndEvent();
#endif

        // 次のエフェクトのために RTV を解除し、A と B を入れ替える (Ping-Pong)
        dc->OMSetRenderTargets(1, &nullRTV, nullptr);
        std::swap(readIdx, writeIdx);
    }

    // 最終的な HDR 結果は readIdx のキャンバスに入っている
    RenderTarget* finalHDR = ResourceManager::Instance().GetRenderTarget(_hdrBuffers[readIdx]);

    // ================================================================
    // ToneMapping (分離したクラスに委譲)
    // ================================================================
    D3D11_VIEWPORT vpSDR = { 0, 0, (float)outputSDR->GetWidth(), (float)outputSDR->GetHeight(), 0.0f, 1.0f };
    dc->RSSetViewports(1, &vpSDR);

    ID3D11RenderTargetView* finalRTV = targetRTV ? targetRTV : outputSDR->GetRTV();

    // ハードコードが消え、スッキリ1行に！
    _toneMapEffect->Process(dc, finalHDR->GetSRV(), finalRTV, toneMapConfig);

    return outputSDR->GetSRV();
}