#include "PostProcessManager.h"
#include "Engine/Graphics/RHI/GpuResourceUtils.h"
#include "Engine/Graphics/Shader/ShaderResources.h"

#include <d3d11_1.h> // GPUマーカー用

// 各種エフェクトクラスのインクルード
#include "BloomEffect.h"
#include "DistortionEffect.h"
#include "RadialBlurEffect.h"
#include "DepthOfFieldEffect.h"
#include "VignetteEffect.h"
#include "ChromaticAberrationEffect.h"
#include "ColorGradingEffect.h"
#include "SSAOEffect.h"
#include "ContactShadowEffect.h"


void PostProcessManager::Initialize(ID3D11Device* device, UINT width, UINT height)
{
    m_device = device;

    // 先に実サイズを確定させる。AddEffect() は各エフェクトの Initialize に
    //   m_width/m_height を渡すため、ここで設定しておかないと 0×0 で中間RTの作成に
    //   失敗し「RenderTarget::Create invalid args (w=0 h=0)」がログに出てしまう。
    m_width  = width;
    m_height = height;

    m_toneMapEffect = std::make_unique<ToneMapEffect>();
    m_toneMapEffect->Initialize(device);

    // ==========================================================
    // 2. エフェクトの生産ラインを構築
    // ==========================================================

    AddEffect<SSAOEffect>();
    AddEffect<ContactShadowEffect>();
    AddEffect<DistortionEffect>();
    AddEffect<RadialBlurEffect>();
    AddEffect<DepthOfFieldEffect>();
    AddEffect<BloomEffect>();
    AddEffect<ChromaticAberrationEffect>();
    AddEffect<VignetteEffect>();
    AddEffect<ColorGradingEffect>();

    Resize(device, width, height);
}

PostProcessManager::~PostProcessManager()
{
    m_effects.clear(); // 各 IPostEffect が自前の RT を解放する

    if (m_hdrBuffers[0].IsValid())  ResourceManager::Instance().UnloadRenderTarget(m_hdrBuffers[0]);
    if (m_hdrBuffers[1].IsValid())  ResourceManager::Instance().UnloadRenderTarget(m_hdrBuffers[1]);
    if (m_outputSDR.IsValid())      ResourceManager::Instance().UnloadRenderTarget(m_outputSDR);
    if (m_sceneColorCopy.IsValid()) ResourceManager::Instance().UnloadRenderTarget(m_sceneColorCopy);
}

void PostProcessManager::Resize(ID3D11Device* device, UINT width, UINT height)
{
    if (width == 0 || height == 0) return;
    m_width = width;
    m_height = height;

    if(m_hdrBuffers[0].IsValid()) ResourceManager::Instance().UnloadRenderTarget(m_hdrBuffers[0]);
    if (m_hdrBuffers[1].IsValid()) ResourceManager::Instance().UnloadRenderTarget(m_hdrBuffers[1]);
    if (m_outputSDR.IsValid()) ResourceManager::Instance().UnloadRenderTarget(m_outputSDR);
    if (m_sceneColorCopy.IsValid()) ResourceManager::Instance().UnloadRenderTarget(m_sceneColorCopy);

    m_hdrBuffers[0] = ResourceManager::Instance().CreateRenderTarget(width, height, DXGI_FORMAT_R16G16B16A16_FLOAT);
    m_hdrBuffers[1] = ResourceManager::Instance().CreateRenderTarget(width, height, DXGI_FORMAT_R16G16B16A16_FLOAT);
    m_outputSDR = ResourceManager::Instance().CreateRenderTarget(width, height, DXGI_FORMAT_R8G8B8A8_UNORM);
    m_sceneColorCopy = ResourceManager::Instance().CreateRenderTarget(width, height, DXGI_FORMAT_R16G16B16A16_FLOAT);

    // 全エフェクトの中間 RT もリサイズ
    for (auto& effect : m_effects) {
        effect->Resize(device, width, height);
    }
}

void PostProcessManager::BeginScene(ID3D11DeviceContext* dc,
    const float          clearColor[4],
    ID3D11DepthStencilView* dsv)
{
    // 初期描画は A (m_hdrBuffers[0]) に行う
    RenderTarget* sceneHDR = ResourceManager::Instance().GetRenderTarget(m_hdrBuffers[0]);
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

ID3D11ShaderResourceView* PostProcessManager::EndScene(ID3D11DeviceContext* dc,
    ID3D11RenderTargetView* targetRTV,
    ID3D11ShaderResourceView* sceneDepthSRV,
    ID3D11ShaderResourceView* sceneNormalSRV)
{
    RenderTarget* outputSDR = ResourceManager::Instance().GetRenderTarget(m_outputSDR);
    if (!outputSDR) return nullptr;

    // RTV 登録を解除
    ID3D11RenderTargetView* nullRTV = nullptr;
    dc->OMSetRenderTargets(1, &nullRTV, nullptr);

    if (sceneDepthSRV) {
        dc->PSSetShaderResources(SLOT_SRV_SCENE_DEPTH, 1, &sceneDepthSRV);
    }
    if (sceneNormalSRV) {
        dc->PSSetShaderResources(SLOT_SRV_SCENE_NORMAL, 1, &sceneNormalSRV);
    }

    // ================================================================
    // Ping-Pong 実行：2枚のRTを読み書きで入れ替えながらパスを重ねる
    // ================================================================
    int readIdx = 0;
    int writeIdx = 1;


    // 描画ループの直前・直後でこれを確認してください
    D3D11_VIEWPORT vpHDR = {};
    vpHDR.Width = static_cast<float>(m_width);  // メンバ変数の幅
    vpHDR.Height = static_cast<float>(m_height); // メンバ変数の高さ
    vpHDR.MinDepth = 0.0f;
    vpHDR.MaxDepth = 1.0f;
    dc->RSSetViewports(1, &vpHDR);


#if defined(ENABLE_EDITOR)
    // 1回だけ Query して、存在する場合のみポインタを保持
    Microsoft::WRL::ComPtr<ID3DUserDefinedAnnotation> annotation;
    dc->QueryInterface(IID_PPV_ARGS(annotation.GetAddressOf()));
#endif

    for (auto& effect : m_effects) {
        if (!effect->IsEnabled()) continue;

        // GPUマーカー（デバッグビルド時のみ）
#if defined(ENABLE_EDITOR)
        if (annotation) {
            std::wstring wname(effect->GetName(), effect->GetName() + strlen(effect->GetName()));
            annotation->BeginEvent(wname.c_str());
        }
#endif

        RenderTarget* readRT = ResourceManager::Instance().GetRenderTarget(m_hdrBuffers[readIdx]);
        RenderTarget* writeRT = ResourceManager::Instance().GetRenderTarget(m_hdrBuffers[writeIdx]);

        dc->RSSetViewports(1, &vpHDR);

        // A を読み込み B に書く
        effect->Process(dc, readRT->GetSRV(), writeRT->GetRTV());

        // マーカー終了
#if defined(ENABLE_EDITOR)
        if (annotation) annotation->EndEvent();
#endif

        // 次のエフェクトのために RTV を解除し、A と B を入れ替える (Ping-Pong)
        dc->OMSetRenderTargets(1, &nullRTV, nullptr);
        std::swap(readIdx, writeIdx);
    }

    // 最終的な HDR 結果は readIdx のキャンバスに入っている
    ID3D11ShaderResourceView* nullDepth = nullptr;
    dc->PSSetShaderResources(SLOT_SRV_SCENE_DEPTH, 1, &nullDepth);
    ID3D11ShaderResourceView* nullNormal = nullptr;
    dc->PSSetShaderResources(SLOT_SRV_SCENE_NORMAL, 1, &nullNormal);

    RenderTarget* finalHDR = ResourceManager::Instance().GetRenderTarget(m_hdrBuffers[readIdx]);

    // ================================================================
    // ToneMapping (分離したクラスに委譲)
    // ================================================================
    D3D11_VIEWPORT vpSDR = { 0, 0, (float)outputSDR->GetWidth(), (float)outputSDR->GetHeight(), 0.0f, 1.0f };
    dc->RSSetViewports(1, &vpSDR);

    ID3D11RenderTargetView* finalRTV = targetRTV ? targetRTV : outputSDR->GetRTV();

    // ハードコードが消え、スッキリ1行に！
    m_toneMapEffect->Process(dc, finalHDR->GetSRV(), finalRTV, toneMapConfig);

    return outputSDR->GetSRV();
}

void PostProcessManager::GrabSceneColor(ID3D11DeviceContext* dc)
{
    RenderTarget* scene = ResourceManager::Instance().GetRenderTarget(m_hdrBuffers[0]);
    RenderTarget* copy  = ResourceManager::Instance().GetRenderTarget(m_sceneColorCopy);
    if (!scene || !copy) return;

    ID3D11Resource* src = nullptr;
    ID3D11Resource* dst = nullptr;
    scene->GetSRV()->GetResource(&src);
    copy->GetSRV()->GetResource(&dst);
    if (src && dst) dc->CopyResource(dst, src);
    if (src) src->Release();
    if (dst) dst->Release();

    ID3D11ShaderResourceView* srv = copy->GetSRV();
    dc->PSSetShaderResources(SLOT_SRV_SCENE_COLOR, 1, &srv);
}

ID3D11ShaderResourceView* PostProcessManager::GetSceneColorSRV() const
{
    RenderTarget* copy = ResourceManager::Instance().GetRenderTarget(m_sceneColorCopy);
    return copy ? copy->GetSRV() : nullptr;
}
