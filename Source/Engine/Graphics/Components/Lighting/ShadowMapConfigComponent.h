#pragma once
// ===========================================================================
// 概要: カスケードシャドウの設定を持つシングルトンコンポーネント。シーンに1つ置く。
//
// 責務:
//   - シャドウバイアス・影の色・カスケードの分割距離を持つ
//
// 備考: ShadowMapRenderSystem が毎フレーム読み、シャドウマップへ渡す。
//       カスケードの数は ShaderShared.h の SHADOW_CASCADE_COUNT（3）で固定。
// ===========================================================================
#include <DirectXMath.h>

struct ShadowMapConfigComponent
{
    float             shadowBias  = 0.0015f;                  // 深度比較のずらし量。小さすぎると縞模様、大きすぎると影が浮く
    DirectX::XMFLOAT4 shadowColor = {0.0f, 0.0f, 0.0f, 0.5f}; // 影の色。a が影の濃さ

    DirectX::XMFLOAT3 cascadeSplits = {15.0f, 50.0f, 200.0f}; // 各カスケードが受け持つ、カメラからの最大距離（m）。近い順
};
