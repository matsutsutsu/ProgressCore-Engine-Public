#pragma once
#include <DirectXMath.h>
#include <string>

// UI の画像、または単色の矩形の見た目。texturePath が空なら color で塗る
// UIImageCollectSystem が UITransformComponent の矩形と合わせて積み、UIRenderer が 2D パスで描く
struct UIImageComponent
{
    std::string       texturePath;                      // 画像パス。空なら単色塗り
    DirectX::XMFLOAT4 color = {1.0f, 1.0f, 1.0f, 1.0f}; // 乗算カラー（単色塗り時はこの色そのもの）

    // 縦グラデーション。useGradient=true で color（上端）→ colorBottom（下端）に補間する
    bool              useGradient = false;
    DirectX::XMFLOAT4 colorBottom = {1.0f, 1.0f, 1.0f, 1.0f};

    // テクスチャの切り出し範囲（正規化 [0,1]。xy = 左上 / zw = 幅と高さ）。既定は全体
    //   矩形の幅と一緒に縮めると、画像を引き伸ばさずに端から隠せる（タイトルロゴが左から現れる演出）
    DirectX::XMFLOAT4 uvRect = {0.0f, 0.0f, 1.0f, 1.0f};

    bool additive  = false; // 加算合成で描くか（発光表現）
    bool visible   = true; // 描画するか
    int  sortOrder = 0;    // 描画順（大きいほど手前）
};
