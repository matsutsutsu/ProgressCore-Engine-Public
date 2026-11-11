// Shader/Pass/LodDebug/LodDebugPS.hlsl
//
// LOD の段の色分け表示用のピクセルシェーダー（LodDebugPass が使う）。
// 頂点シェーダーは NormalPrepassVS をそのまま使うので、入力はその出力と同じ形にする
// （受け取る形が違うと、シェーダーをつなげられず何も描かれない）。
#include "Engine/Graphics/Shader/ShaderResources.h"

struct NORMAL_VS_OUT
{
    float4 vertex     : SV_POSITION;
    float3 viewNormal : NORMAL;
};

cbuffer CbLodDebug : register(REG_CB(SLOT_CB_MATERIAL))
{
    float4 gLodColor; // rgb = 段の色、a = 重ねる濃さ
};

float4 main(NORMAL_VS_OUT pin) : SV_TARGET
{
    return gLodColor;
}
