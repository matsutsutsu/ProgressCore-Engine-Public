// スプライトと UI のゲージが共有する、頂点シェーダーの出力
struct VS_OUT
{
	float4 position : SV_POSITION;
	float4 color    : COLOR;    // 頂点の色。テクスチャの色に掛ける
	float2 texcoord : TEXCOORD;
};
