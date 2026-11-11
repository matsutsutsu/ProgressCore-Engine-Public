#include "UIGaugeSprite.h"

#include "Engine/Core/Math/Misc.h"
#include "Engine/Graphics/RHI/GpuResourceUtils.h"

#include <algorithm>

UIGaugeSprite::UIGaugeSprite(ID3D11Device *device)
    : Sprite(device) // 頂点シェーダー・頂点バッファ・白のダミーテクスチャは Sprite が作る
{
    // Sprite::Render は pixelShader をバインドするので、ここを差し替えればゲージのシェーダーで描かれる
    pixelShader.Reset();
    HRESULT hr = GpuResourceUtils::LoadPixelShader(device, "Assets/Shader/UIGaugePS.cso", pixelShader.GetAddressOf());
    _ASSERT_EXPR(SUCCEEDED(hr), HRTrace(hr));

    hr = GpuResourceUtils::CreateConstantBuffer(device, sizeof(GpuData::CbUIGaugeData), m_cb.GetAddressOf());
    _ASSERT_EXPR(SUCCEEDED(hr), HRTrace(hr));
}



void UIGaugeSprite::RenderGauge(ID3D11DeviceContext *dc, float x, float y, float w, float h,
                                GpuData::CbUIGaugeData data, float alpha)
{
    // 四角形を矩形の外へ広げる量。縁の太さに、輪郭をぼかす 1px と端数の余裕を足す
    //   形の外側に出る演出があれば、そのうち一番はみ出すものに合わせてさらに広げる
    //   （四角形の外は描けないので、足りないと光やずれた部分が途中で切れる）
    float extra = 0.0f;
    if (data.auraColor.w > 0.0f) extra = std::max(extra, data.auraWidth * 3.0f); // 外側の光は広がりの3倍でほぼ消える
    if (data.glitch > 0.0f) extra = std::max(extra, 18.0f);                    // グリッチの横ずれは最大 14px + 色ずれ 3px
    const float pad = data.frameWidth + 2.0f + extra;
    data.sizePx     = {w, h};
    data.padPx      = pad;

    dc->UpdateSubresource(m_cb.Get(), 0, nullptr, &data, 0, 0);
    dc->PSSetConstantBuffers(SLOT_CB_UIGAUGE, 1, m_cb.GetAddressOf());

    // テクスチャ座標は四角形全体で 0〜1 にする（切り抜きをダミーテクスチャ全体にする）
    // シェーダーはこれに四角形の大きさを掛けて px 座標に戻す
    const DirectX::XMFLOAT4 c          = {1.0f, 1.0f, 1.0f, alpha};
    const DirectX::XMFLOAT4 corners[4] = {c, c, c, c};
    Render(dc, x - pad, y - pad, 0.0f, w + pad * 2.0f, h + pad * 2.0f,
           0.0f, 0.0f, GetWidth(), GetHeight(), 0.0f, corners);
}
