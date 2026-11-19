// ===========================================================================
// 概要: HUD のゲージ1本を、四角形1枚の中で描く
//
// 入力: SpriteVS の出力（UV・頂点の色）と、ゲージ1本分の設定（UIShared.h の cbuffer）
// 出力: ゲージの色（ストレートα。UITransparency の合成に合わせる）
//
// 備考: 形（平行四辺形）・縁・外側の光・背景・遅延バー・フィル・区切り・先端の光・光沢・目印を
//       すべて距離（px）で計算するので、どの大きさで描いても輪郭がなめらかになる
//
//       座標の考え方
//         p  … ゲージの矩形の左上を原点にした px 座標（縁・外側の光・グリッチの分だけ外側にもはみ出す）
//         xs … 傾きを打ち消した横位置。上辺ほど右へずれている分を引くので、
//              xs = 0 が左の斜辺、xs = L が右の斜辺になる。割合は xs / L
//
//       関数の役割
//         main  … 被弾直後のグリッチ中だけ、Shade を横にずらして3回呼び、R・G・B を別々の位置から取る
//         Shade … 外側の光 → 縁 → 中身（Body）の順に重ねる
//         Body  … 背景 → 遅延バー → 回復の予告 → フィル → 区切り → 目印 の順に重ねる
// ===========================================================================
#include "Shader/Material/Sprite/Sprite.hlsli"
#include "Engine/Graphics/Shader/Shared/UIShared.h"

// 時間で進む演出の長さ（秒）。経過時間は hitAge / healAge で受け取る
static const float kGlitchTime = 0.30f; // 被弾のグリッチが収まるまで
static const float kHealTime   = 0.60f; // 回復した分に光が満ちるまで

// 格子の1マスごとに決まった乱数 [0,1)。グリッチの帯ごとのずれ幅に使う
float Hash21(float2 p)
{
    p = frac(p * float2(123.34f, 456.21f));
    p += dot(p, p + 45.32f);
    return frac(p.x * p.y);
}

// ストレートαどうしの「上に重ねる」合成。出力もストレートα（UITransparency の合成に合わせる）
float4 Over(float4 dst, float4 src)
{
    float  a = src.a + dst.a * (1.0f - src.a);
    float3 c = (src.rgb * src.a + dst.rgb * dst.a * (1.0f - src.a)) / max(a, 1e-5f);
    return float4(c, a);
}

// ゲージの中身（背景〜目印）の色。形の内側かどうかは呼び出し側が掛ける
float4 Body(float2 p, float h, float L, float k)
{
    float xs   = p.x - slantPx * (1.0f - p.y / h);
    float edge = value * L; // フィルの先端の位置（px）

    // 回復中は、光の前線（front）までしか塗らない。前線は healFrom から先端へ減速しながら進む
    float healT   = saturate(healAge / kHealTime);
    bool  healing = healAge < kHealTime && value > healFrom;
    float front   = edge;
    if (healing)
        front = min(edge, lerp(healFrom * L, edge, 1.0f - (1.0f - healT) * (1.0f - healT)));

    float fillM = (value > 0.0f) ? saturate(front - xs + 0.5f) : 0.0f;
    float pastM = 1.0f - saturate(edge - xs + 0.5f); // 本来の先端より右か（遅延バーが見える範囲）

    float4 body = bgColor;

    // --- 遅延バー（失った分）: 先端から trailValue までの帯 ---
    float trailM = saturate(trailValue * L - xs + 0.5f) * pastM;
    body         = Over(body, float4(trailColor.rgb, trailColor.a * trailM));

    // --- 回復の予告: 光がまだ届いていない回復分を薄く見せる ---
    if (healing)
    {
        float pv = saturate(edge - xs + 0.5f) * (1.0f - fillM);
        body     = Over(body, float4(fillColor.rgb, 0.3f * pv));
    }

    // --- フィルの色 ---
    float3 fill = fillColor.rgb * (1.0f - gradient * saturate(p.y / h));

    // 光沢: 斜めの帯が4秒ごとに左から右へ流れる
    if (sheen > 0.0f)
    {
        float band  = h * 1.5f;
        float sweep = frac(time * 0.25f) * (L + band * 6.0f) - band * 3.0f;
        float x     = (xs - sweep) / band;
        fill += sheen * exp(-x * x);
    }

    // 先端の光: 減っている途中のゲージだけ（満タンと空では先端が無い）
    if (value > 0.001f && value < 0.999f)
    {
        float e = abs(xs - front);
        fill += glowColor.rgb * glowColor.a * exp(-e / max(glowWidth, 0.1f));
        fill = lerp(fill, 1.0f, saturate(1.0f - e * 0.75f) * 0.6f); // 先端の 1〜2px は白く締める
    }

    // 回復の前線を白く光らせる
    if (healing)
        fill += exp(-abs(xs - front) / 3.0f) * (1.0f - healT) * 0.9f;

    fill += flash;
    body = Over(body, float4(saturate(fill), fillColor.a * fillM));

    // --- 区切り: 隙間は背景の色に戻す（傾きに沿った斜めの線になる） ---
    if (segments > 1.0f)
    {
        float segW = L / segments;
        float s    = abs(xs - segW * round(xs / segW)) * k;
        float gap  = saturate(gapPx * 0.5f + 0.5f - s);
        gap *= step(segW * 0.5f, xs) * step(xs, L - segW * 0.5f); // 両端の斜辺には引かない
        body = lerp(body, bgColor, gap);
    }

    // --- 目印の線 ---
    if (marker >= 0.0f)
    {
        float m = abs(xs - marker * L) * k;
        body    = Over(body, float4(1.0f, 1.0f, 1.0f, 0.9f * saturate(markerWidth * 0.5f + 0.5f - m)));
    }

    return body;
}

// ゲージ1ピクセル分の色（ストレートα）
float4 Shade(float2 p)
{
    float h  = max(sizePx.y, 1.0f);
    float L  = max(sizePx.x - slantPx, 1.0f);
    float xs = p.x - slantPx * (1.0f - p.y / h);

    // 斜辺は横方向の距離のままだと傾きの分だけ太って見えるので、斜辺に垂直な距離へ直す係数
    float k = h / sqrt(h * h + slantPx * slantPx);

    // --- 形: 平行四辺形までの距離（内側が負、px） ---
    float d      = max(max(-xs, xs - L) * k, max(-p.y, p.y - h));
    float inside = saturate(0.5f - d); // 1px 幅でぼかす

    // --- 外側の光: 形から離れるほど弱まる ---
    float4 col = float4(0.0f, 0.0f, 0.0f, 0.0f);
    if (auraColor.a > 0.0f && auraWidth > 0.0f)
        col = float4(auraColor.rgb, saturate(auraColor.a * exp(-max(d, 0.0f) / auraWidth)) * (1.0f - inside));

    // --- 縁（形の外側の帯だけ） ---
    float outer = saturate(0.5f - (d - frameWidth));
    col         = Over(col, float4(frameColor.rgb, frameColor.a * outer * (1.0f - inside)));

    // --- 中身 ---
    float4 body = Body(p, h, L, k);
    return Over(col, float4(body.rgb, body.a * inside));
}

float4 main(VS_OUT pin) : SV_TARGET
{
    float2 p = pin.texcoord * (sizePx + 2.0f * padPx) - padPx;

    // --- 被弾のグリッチ: 2.5px の横帯ごとに乱数で横へずらし、R と B を逆向きに少しずらす ---
    //   ずれ幅を3乗するのは、ほとんどの帯は少しだけずれ、たまに大きくずれる帯が混ざるようにするため
    float g = (glitch > 0.0f && hitAge < kGlitchTime) ? glitch * pow(1.0f - hitAge / kGlitchTime, 2.0f) : 0.0f;

    float4 col;
    [branch] if (g > 0.001f)
    {
        float  r    = Hash21(float2(floor(p.y / 2.5f), floor(time * 45.0f))) * 2.0f - 1.0f;
        float2 base = p - float2(r * r * r * 14.0f * g, 0.0f);
        float2 ch   = float2(3.0f * g, 0.0f);
        float4 cr   = Shade(base - ch);
        float4 cg   = Shade(base);
        float4 cb   = Shade(base + ch);
        float  a    = max(max(cr.a, cg.a), cb.a);
        col         = float4(float3(cr.r * cr.a, cg.g * cg.a, cb.b * cb.a) / max(a, 1e-5f), a);
    }
    else
    {
        col = Shade(p);
    }

    return col * pin.color; // 頂点色は出現アニメなどで全体を薄くするのに使う
}
