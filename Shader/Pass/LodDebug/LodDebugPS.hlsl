// ===========================================================================
// 概要: LOD の段の色分け表示（LodDebugPass）。メッシュを段ごとの色で塗る
//
// 入力: NormalPrepassVS の出力（使わない）
// 出力: 段の色（α は重ねる濃さ）
//
// 備考: 頂点シェーダーは NormalPrepassVS を使うので、入力はその出力と同じ形にする
//       形が違うとシェーダーをつなげられず、何も描かれない
// ===========================================================================
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
