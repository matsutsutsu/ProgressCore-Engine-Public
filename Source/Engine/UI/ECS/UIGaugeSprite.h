#pragma once
// ===========================================================================
// 概要: HUD のゲージ1本を UIGaugePS で描く Sprite の派生クラス UIGaugeSprite
//
// 責務:
//   - ピクセルシェーダーを UIGaugePS に差し替え、ゲージの定数バッファを持つ
//   - ゲージの矩形を縁の分だけ広げた四角形1枚で描く
//
// 備考: 頂点シェーダー・頂点バッファ・座標の変換は Sprite のものをそのまま使う
//       形・縁・遅延バー・区切りなどは全部シェーダーが描くので、テクスチャは使わない（白のダミーのまま）
// ===========================================================================
#include "Engine/Graphics/Renderer/Sprite.h"
#include "Engine/Graphics/Shader/Shared/UIShared.h"

class UIGaugeSprite : public Sprite
{
public:
    explicit UIGaugeSprite(ID3D11Device *device);

    // ゲージを1本描く。矩形は px で縁を含まない。data の sizePx と padPx はここで上書きする
    void RenderGauge(ID3D11DeviceContext *dc, float x, float y, float w, float h,
                     GpuData::CbUIGaugeData data, float alpha);

private:
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_cb; // CbUIGaugeData
};
