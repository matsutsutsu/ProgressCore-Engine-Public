#pragma once
#include <DirectXMath.h>

// カスケードシャドウの設定。シーンに1つ置き、ShadowMapRenderSystem が毎フレーム読む
// カスケードの数は ShaderShared.h の SHADOW_CASCADE_COUNT で固定
struct ShadowMapConfigComponent
{
    float             shadowBias  = 0.0015f;                  // 深度比較のずらし量。小さすぎると縞模様、大きすぎると影が浮く
    DirectX::XMFLOAT4 shadowColor = {0.0f, 0.0f, 0.0f, 0.5f}; // 影の色。a が影の濃さ

    DirectX::XMFLOAT3 cascadeSplits = {15.0f, 50.0f, 200.0f}; // 各カスケードが受け持つ、カメラからの最大距離（m）。近い順
};
