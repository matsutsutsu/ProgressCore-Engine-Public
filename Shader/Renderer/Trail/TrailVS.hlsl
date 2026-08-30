// Shader/Renderer/Trail/TrailVS.hlsl
// 武器トレイル用の最小頂点シェーダ。加算レイヤと歪みレイヤで共用する。
// ワールド空間で積まれた頂点を viewProjection で投影するだけ。
// viewProjection は C++ 側から転置なしでアップロードされるため row_major で受ける
// （エンジン共通の ShaderShared.h と同じ規約）。
//
// worldPos を渡すのは歪みレイヤのため。ピクセル側で ddx/ddy を取ると
// 面の法線が求まるので、頂点フォーマットに法線を足さずに屈折方向を作れる。
// 加算レイヤ(TrailPS)はこの出力を読まないが、余分な VS 出力は許容される。

cbuffer CbTrail : register(b0)
{
    row_major float4x4 viewProjection;
};

struct VSInput
{
    float3 position : POSITION;
    float4 color    : COLOR;
    float2 uv       : TEXCOORD0;
};

struct VSOutput
{
    float4 position : SV_POSITION;
    float4 color    : COLOR;
    float3 worldPos : TEXCOORD0;
    float2 uv       : TEXCOORD1;
};

VSOutput main(VSInput input)
{
    VSOutput output;
    output.position = mul(float4(input.position, 1.0f), viewProjection);
    output.color    = input.color;
    output.worldPos = input.position;
    output.uv       = input.uv;
    return output;
}
