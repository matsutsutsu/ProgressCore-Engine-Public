// UI の 2D パスで C++ と HLSL が共有する定数バッファ（ゲージの UIGaugePS）
// C++ では GpuData::CbUIGaugeData、HLSL では cbuffer になる。HLSL も読むのでインクルードガードを使う
#ifndef UI_SHARED_H
#define UI_SHARED_H

#include "Engine/Graphics/Shader/ShaderResources.h"

#ifdef __cplusplus
#include <DirectXMath.h>
namespace GpuData
{
    using float2 = DirectX::XMFLOAT2;
    using float4 = DirectX::XMFLOAT4;
#define UI_STRUCT(name, slot) struct name
#else
#define UI_STRUCT(name, slot) cbuffer name : register(REG_CB(slot))
#endif

    // ゲージ1本分（192 バイト）。長さは px、割合は [0,1]、時間は秒
    UI_STRUCT(CbUIGaugeData, SLOT_CB_UIGAUGE)
    {
        float2 sizePx;  // ゲージの矩形の幅・高さ（縁は含まない）
        float  slantPx; // 上辺を右へずらす量。0 なら長方形、正なら「/」に傾いた平行四辺形
        float  padPx;   // 描く四角形を矩形の外へ広げた量（縁とアンチエイリアスの分）。書くのは UIGaugeSprite

        float value;      // フィルの割合
        float trailValue; // 遅延バーの割合（value〜trailValue が失った分）
        float segments;   // 区切りの数。1 以下なら区切らない
        float gapPx;      // 区切りの隙間の幅

        float frameWidth; // 縁の太さ。0 なら描かない
        float glowWidth;  // フィル先端の光の広がり
        float flash;      // 被弾したときにフィル全体を白く飛ばす量 [0,1]
        float time;       // 光沢が流れる位相（秒）

        float marker;      // 目印の線を引く割合。負なら引かない（ボスの形態移行の HP など）
        float markerWidth; // 目印の線の太さ
        float sheen;       // 光沢の強さ。0 なら流さない
        float gradient;    // フィルの下端をどれだけ暗くするか [0,1]

        float hitAge;   // 最後に被弾してからの秒数（グリッチの進み具合）
        float healFrom; // 最後に回復する前の割合（光が満ちていく範囲は healFrom〜value）
        float healAge;  // 最後に回復してからの秒数
        float glitch;   // 被弾したときのグリッチの強さ。0 なら出さない

        float auraWidth; // 形の外側ににじむ光の広がり
        float pad0;      // 以下3つは 16 バイト境界までの詰め物。配列にしないのは、HLSL の cbuffer では
        float pad1;      //   配列の要素が1つずつ 16 バイト境界に置かれ、C++ 側と配置がずれるため
        float pad2;

        float4 bgColor;    // 背景
        float4 fillColor;  // フィル
        float4 trailColor; // 遅延バー（失った分）
        float4 frameColor; // 縁
        float4 glowColor;  // フィル先端の光（rgb = 色、a = 強さ）
        float4 auraColor;  // 外側の光（rgb = 色、a = 強さ。0 なら出さない）
    };

#undef UI_STRUCT

#ifdef __cplusplus
    static_assert(sizeof(CbUIGaugeData) == 192, "CbUIGaugeData のサイズが HLSL の cbuffer と一致しない");
} // namespace GpuData
#endif

#endif // UI_SHARED_H
