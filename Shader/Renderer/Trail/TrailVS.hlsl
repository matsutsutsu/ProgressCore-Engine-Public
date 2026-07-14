// Shader/Renderer/Trail/TrailVS.hlsl
// 武器トレイル用の最小頂点シェーダ。
// ワールド空間で積まれた頂点を viewProjection で投影するだけ。
// viewProjection は C++ 側から転置なしでアップロードされるため row_major で受ける
// （エンジン共通の ShaderShared.h と同じ規約）。

cbuffer CbTrail : register(b0)
{
    row_major float4x4 viewProjection;
};

struct VSInput
{
    float3 position : POSITION;
    float4 color    : COLOR;
};

struct VSOutput
{
    float4 position : SV_POSITION;
    float4 color    : COLOR;
};

VSOutput main(VSInput input)
{
    VSOutput output;
    output.position = mul(float4(input.position, 1.0f), viewProjection);
    output.color    = input.color;
    return output;
}
