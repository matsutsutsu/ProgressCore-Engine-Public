// Shader/Renderer/Trail/TrailPS.hlsl
// 加算ブレンド(SrcAlpha, One)前提の最小ピクセルシェーダ。
// 頂点カラーをそのまま出力し、アルファ(尾ほど小さい)が加算強度になる。
// rgb は事前乗算しない（ブレンドの SrcAlpha が強度を掛けるため）。

struct PSInput
{
    float4 position : SV_POSITION;
    float4 color    : COLOR;
};

float4 main(PSInput input) : SV_TARGET
{
    return input.color;
}
