#pragma once
// アニメ調整用の床に付ける目盛り。ルートモーションの前進量や足の滑りを目で測るためのもの
// 描くのは PreviewGridDrawSystem（エディタのみ）。中心は付けた Entity の位置で、向きはワールドの軸にそろえる
#include <DirectXMath.h>

struct PreviewGridComponent
{
    bool              visible    = true;
    float             spacing    = 1.0f; // 線の間隔 [m]
    int               halfCount  = 20;   // 中心から片側に引く本数
    int               majorEvery = 5;    // 何本ごとに太線の色にするか
    DirectX::XMFLOAT4 color      = {0.2f, 0.2f, 0.2f, 1.0f};
    DirectX::XMFLOAT4 majorColor = {0.4f, 0.4f, 0.4f, 1.0f};
};
