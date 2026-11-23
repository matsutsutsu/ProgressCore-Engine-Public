// ===========================================================================
// 概要: 空（SkyboxPass）の頂点シェーダー。画面を覆う三角形を作り、各頂点の視線の向きを求める
//
// 入力: 頂点の番号だけ（頂点バッファなしで3頂点を描く）
// 出力: 画面の位置・ワールド空間の視線の向き
//
// 備考: 深度を一番奥（1.0）に固定するので、物が描かれていない所にだけ空が残る
// ===========================================================================
#include "Shader/Common/Scene.hlsli"

// SkyboxPass.h の CbSkybox・SkyboxPS.hlsl と同じ並び
cbuffer CbSkybox : register(REG_CB(SLOT_CB_SKYBOX))
{
    row_major float4x4 invViewProj; // 画面の位置からワールドの位置へ戻す行列
    float skyIntensity; // PS で使う
    float3 skyPadding;
};

struct VS_OUT
{
    float4 pos : SV_POSITION;
    float3 rayDir : TEXCOORD;
};

VS_OUT main(uint vertexID : SV_VertexID)
{
    VS_OUT vout;

    // FullScreenVS と同じ、画面を覆う大きな三角形
    float2 texcoord = float2((vertexID << 1) & 2, vertexID & 2);
    float2 clipPos = texcoord * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f);

    // z と w を 1 にして、透視の割り算の後も深度が 1.0（一番奥）のままになるようにする
    vout.pos = float4(clipPos, 1.0f, 1.0f);

    // 画面の一番奥の点をワールドへ戻し、カメラからそこへの向きを視線にする
    float4 worldPos = mul(float4(clipPos, 1.0f, 1.0f), invViewProj);
    vout.rayDir = (worldPos.xyz / worldPos.w) - cameraPosition.xyz;

    return vout;
}