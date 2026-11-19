// ===========================================================================
// 概要: 法線プリパス（NormalPrepass）。ビュー空間の法線の XY を書く
//
// 入力: NormalPrepassVS の出力（ビュー空間の法線）
// 出力: 法線の XY（-1〜1）を RG16_FLOAT に書く
//
// 備考: Z は書かない。読む側（DepthUtils.hlsli の SampleSceneViewNormal）が XY から戻す
// ===========================================================================
struct NORMAL_VS_OUT
{
    float4 vertex     : SV_POSITION;
    float3 viewNormal : NORMAL;
};

float2 main(NORMAL_VS_OUT pin) : SV_TARGET
{
    return normalize(pin.viewNormal).xy;
}
