#include "PostProcessManager.h"
#include "Engine/Graphics/RHI/GpuResourceUtils.h"
#include "Engine/Graphics/Shader/ShaderResources.h"
#include "Engine/Platform/Core/Logger.h"

#include <d3d11_1.h> // ID3DUserDefinedAnnotation

#include "BloomEffect.h"
#include "DistortionEffect.h"
#include "RadialBlurEffect.h"
#include "DepthOfFieldEffect.h"
#include "VignetteEffect.h"
#include "ChromaticAberrationEffect.h"
#include "ColorGradingEffect.h"
#include "PauseBlurEffect.h"
#include "SSAOEffect.h"

// ---------------------------------------------------------------------------
// 初期化とリサイズ
// ---------------------------------------------------------------------------

void PostProcessManager::Initialize(ID3D11Device *device, UINT width, UINT height)
{
    m_device = device;

    // AddEffect は各エフェクトの Initialize に m_width / m_height を渡すので、先に入れておく
    // （入れないと 0×0 でレンダーターゲットを作ろうとして失敗する）
    m_width  = width;
    m_height = height;

    m_toneMapEffect = std::make_unique<ToneMapEffect>();
    m_toneMapEffect->Initialize(device);

    // かける順
    AddEffect<SSAOEffect>();
    AddEffect<DistortionEffect>();
    AddEffect<RadialBlurEffect>();
    AddEffect<DepthOfFieldEffect>();
    AddEffect<BloomEffect>();
    AddEffect<ChromaticAberrationEffect>();
    AddEffect<VignetteEffect>();
    AddEffect<ColorGradingEffect>();
    // 最後に置き、ブルームや色の補正まで済んだ絵をぼかす。
    // UI はポストプロセスの後に描くので、メニューはぼけない
    AddEffect<PauseBlurEffect>();

    Resize(device, width, height);
}

PostProcessManager::~PostProcessManager()
{
    m_effects.clear(); // 各エフェクトが自分のレンダーターゲットを解放する

    if (m_hdrBuffers[0].IsValid()) ResourceManager::Instance().UnloadRenderTarget(m_hdrBuffers[0]);
    if (m_hdrBuffers[1].IsValid()) ResourceManager::Instance().UnloadRenderTarget(m_hdrBuffers[1]);
    if (m_outputSDR.IsValid()) ResourceManager::Instance().UnloadRenderTarget(m_outputSDR);
    if (m_sceneColorCopy.IsValid()) ResourceManager::Instance().UnloadRenderTarget(m_sceneColorCopy);
}

void PostProcessManager::Resize(ID3D11Device *device, UINT width, UINT height)
{
    if (width == 0 || height == 0) return;
    m_width  = width;
    m_height = height;

    if (m_hdrBuffers[0].IsValid()) ResourceManager::Instance().UnloadRenderTarget(m_hdrBuffers[0]);
    if (m_hdrBuffers[1].IsValid()) ResourceManager::Instance().UnloadRenderTarget(m_hdrBuffers[1]);
    if (m_outputSDR.IsValid()) ResourceManager::Instance().UnloadRenderTarget(m_outputSDR);
    if (m_sceneColorCopy.IsValid()) ResourceManager::Instance().UnloadRenderTarget(m_sceneColorCopy);

    m_hdrBuffers[0]  = ResourceManager::Instance().CreateRenderTarget(width, height, DXGI_FORMAT_R16G16B16A16_FLOAT);
    m_hdrBuffers[1]  = ResourceManager::Instance().CreateRenderTarget(width, height, DXGI_FORMAT_R16G16B16A16_FLOAT);
    m_outputSDR      = ResourceManager::Instance().CreateRenderTarget(width, height, DXGI_FORMAT_R8G8B8A8_UNORM);
    m_sceneColorCopy = ResourceManager::Instance().CreateRenderTarget(width, height, DXGI_FORMAT_R16G16B16A16_FLOAT);

    for (auto &effect : m_effects)
    {
        effect->Resize(device, width, height);
    }
}

// ---------------------------------------------------------------------------
// シーンの開始と終了（エフェクトの適用）
// ---------------------------------------------------------------------------

void PostProcessManager::BeginScene(ID3D11DeviceContext    *dc,
                                    const float             clearColor[4],
                                    ID3D11DepthStencilView *dsv)
{
    // シーンは [0] に描く
    RenderTarget *sceneHDR = ResourceManager::Instance().GetRenderTarget(m_hdrBuffers[0]);
    if (!sceneHDR) return;

    float w = static_cast<float>(sceneHDR->GetWidth());
    float h = static_cast<float>(sceneHDR->GetHeight());
    if (w <= 0.0f || w > 16384.0f || h <= 0.0f || h > 16384.0f) return;

    sceneHDR->Clear(dc, clearColor);

    ID3D11RenderTargetView *rtv = sceneHDR->GetRTV();
    dc->OMSetRenderTargets(1, &rtv, dsv);

    D3D11_VIEWPORT vp = {};
    vp.Width          = w;
    vp.Height         = h;
    vp.MaxDepth       = 1.0f;
    dc->RSSetViewports(1, &vp);
}

ID3D11ShaderResourceView *PostProcessManager::EndScene(ID3D11DeviceContext      *dc,
                                                       ID3D11RenderTargetView   *targetRTV,
                                                       ID3D11ShaderResourceView *sceneDepthSRV,
                                                       ID3D11ShaderResourceView *sceneNormalSRV)
{
    RenderTarget *outputSDR = ResourceManager::Instance().GetRenderTarget(m_outputSDR);
    if (!outputSDR) return nullptr;

    // 同じ2枚を交互に使うだけなので、ここで1回だけハンドルから引き、null の確認もここでまとめて行う
    RenderTarget *hdr[2] = {
        ResourceManager::Instance().GetRenderTarget(m_hdrBuffers[0]),
        ResourceManager::Instance().GetRenderTarget(m_hdrBuffers[1]),
    };
    if (!hdr[0] || !hdr[1])
    {
        // Resize でレンダーターゲットを作れなかったとき。BeginScene も同じ理由で何もしていないので、絵はもともと無い
        CCL_LOG_ERROR(LogCategory::Render,
                      "PostProcessManager::EndScene: HDR バッファが未生成のためポストプロセスを中止します。");
        return nullptr;
    }

    // HDR バッファを読めるよう、描画先から外す
    ID3D11RenderTargetView *nullRTV = nullptr;
    dc->OMSetRenderTargets(1, &nullRTV, nullptr);

    if (sceneDepthSRV)
    {
        dc->PSSetShaderResources(SLOT_SRV_SCENE_DEPTH, 1, &sceneDepthSRV);
    }
    if (sceneNormalSRV)
    {
        dc->PSSetShaderResources(SLOT_SRV_SCENE_NORMAL, 1, &sceneNormalSRV);
    }

    // 2枚の HDR バッファの読み書きを入れ替えながら、エフェクトを重ねる
    int readIdx  = 0;
    int writeIdx = 1;

    // フル解像度のビューポート（各エフェクトの前で設定し直す）
    D3D11_VIEWPORT vpHDR = {};
    vpHDR.Width          = static_cast<float>(m_width);
    vpHDR.Height         = static_cast<float>(m_height);
    vpHDR.MinDepth       = 0.0f;
    vpHDR.MaxDepth       = 1.0f;
    dc->RSSetViewports(1, &vpHDR);

#if defined(ENABLE_EDITOR)
    Microsoft::WRL::ComPtr<ID3DUserDefinedAnnotation> annotation;
    dc->QueryInterface(IID_PPV_ARGS(annotation.GetAddressOf()));
#endif

    for (auto &effect : m_effects)
    {
        if (!effect->IsEnabled()) continue;

        // GPU マーカー（エディタ入りのビルドだけ）
#if defined(ENABLE_EDITOR)
        if (annotation)
        {
            std::wstring wname(effect->GetName(), effect->GetName() + strlen(effect->GetName()));
            annotation->BeginEvent(wname.c_str());
        }
#endif

        // BloomEffect は縮小バッファに描くのでビューポートを変えたまま戻る。
        // 次のエフェクトがフル解像度で描けるよう、毎回ここで戻す
        dc->RSSetViewports(1, &vpHDR);

        effect->Process(dc, hdr[readIdx]->GetSRV(), hdr[writeIdx]->GetRTV());

#if defined(ENABLE_EDITOR)
        if (annotation) annotation->EndEvent();
#endif

        // 書いた方を次に読めるよう、描画先から外して読み書きを入れ替える
        dc->OMSetRenderTargets(1, &nullRTV, nullptr);
        std::swap(readIdx, writeIdx);
    }

    ID3D11ShaderResourceView *nullDepth = nullptr;
    dc->PSSetShaderResources(SLOT_SRV_SCENE_DEPTH, 1, &nullDepth);
    ID3D11ShaderResourceView *nullNormal = nullptr;
    dc->PSSetShaderResources(SLOT_SRV_SCENE_NORMAL, 1, &nullNormal);

    RenderTarget *finalHDR = hdr[readIdx]; // 最後に書いた方（入れ替えた後なので read 側）

    D3D11_VIEWPORT vpSDR = {0, 0, (float)outputSDR->GetWidth(), (float)outputSDR->GetHeight(), 0.0f, 1.0f};
    dc->RSSetViewports(1, &vpSDR);

    ID3D11RenderTargetView *finalRTV = targetRTV ? targetRTV : outputSDR->GetRTV();

    m_toneMapEffect->Process(dc, finalHDR->GetSRV(), finalRTV, toneMapConfig);

    return outputSDR->GetSRV();
}

// ---------------------------------------------------------------------------
// シーン色のコピー
// ---------------------------------------------------------------------------

void PostProcessManager::GrabSceneColor(ID3D11DeviceContext *dc)
{
    RenderTarget *scene = ResourceManager::Instance().GetRenderTarget(m_hdrBuffers[0]);
    RenderTarget *copy  = ResourceManager::Instance().GetRenderTarget(m_sceneColorCopy);
    if (!scene || !copy) return;

    ID3D11Resource *src = nullptr;
    ID3D11Resource *dst = nullptr;
    scene->GetSRV()->GetResource(&src);
    copy->GetSRV()->GetResource(&dst);
    if (src && dst) dc->CopyResource(dst, src);
    if (src) src->Release();
    if (dst) dst->Release();

    ID3D11ShaderResourceView *srv = copy->GetSRV();
    dc->PSSetShaderResources(SLOT_SRV_SCENE_COLOR, 1, &srv);
}

ID3D11ShaderResourceView *PostProcessManager::GetSceneColorSRV() const
{
    RenderTarget *copy = ResourceManager::Instance().GetRenderTarget(m_sceneColorCopy);
    return copy ? copy->GetSRV() : nullptr;
}
