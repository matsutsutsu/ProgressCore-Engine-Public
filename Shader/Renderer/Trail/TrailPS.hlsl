// Shader/Renderer/Trail/TrailPS.hlsl
// 加算ブレンド(SrcAlpha, One)前提の最小ピクセルシェーダ。
// 頂点カラー × トレイルテクスチャ を出力する。
//   ・テクスチャ未設定のトレイルには C++ 側が 1x1 白を刺すので、白×色＝色そのまま
//     ＝従来の単色トレイルと完全一致（後方互換）。
//   ・テクスチャの α で輪郭形状、RGB で模様、U(長さ)方向グラデで寿命色変化が出せる。
// rgb は事前乗算しない（ブレンドの SrcAlpha が強度を掛けるため）。
#include "Shader/Common/Scene.hlsli"

Texture2D    gTrailTex : register(t0);
SamplerState LinearSampler : register(REG_SMP(SLOT_SMP_LINEAR));

struct PSInput
{
    float4 position : SV_POSITION;
    float4 color    : COLOR;
    float3 worldPos : TEXCOORD0;
    float2 uv       : TEXCOORD1;
};

float4 main(PSInput input) : SV_TARGET
{
    float4 tex = gTrailTex.Sample(LinearSampler, input.uv);
    return input.color * tex;
}
