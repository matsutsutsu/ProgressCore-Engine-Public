#include "Shader/Common/Common.hlsli"

cbuffer CbMaterial : register(REG_CB(SLOT_CB_MATERIAL))
{
    float4 materialColor;
};

Texture2D DiffuseMap : register(REG_SRV(SLOT_SRV_MAT_TEX0));
SamplerState LinearSampler : register(REG_SMP(SLOT_SMP_LINEAR));

float4 main(VS_OUT pin) : SV_TARGET
{
	float4 color = DiffuseMap.Sample(LinearSampler, pin.texcoord) * materialColor;

	float3 N = normalize(pin.normal);
	float3 L = normalize(-lightDirection.xyz);
	float power = max(0, dot(L, N));

	power = power * 0.7 + 0.3f;

	color.rgb *= lightColor.rgb * power;

	return color;
}
