// Shader/Pass/NormalPrepass/NormalPrepassPS.hlsl
//
// 法線プリパス用のピクセルシェーダー。ビュー空間法線の XY のみを RG16_FLOAT へ書く。
struct NORMAL_VS_OUT
{
    float4 vertex     : SV_POSITION;
    float3 viewNormal : NORMAL;
};

float2 main(NORMAL_VS_OUT pin) : SV_TARGET
{
    return normalize(pin.viewNormal).xy;
}
