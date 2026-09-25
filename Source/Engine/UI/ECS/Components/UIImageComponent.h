#pragma once
// ===========================================================================
// 概要: UI の画像（または単色の矩形）の見た目を持つコンポーネント UIImageComponent。
//
// 責務:
//   - 画像パス・色・グラデーション・合成方法・描画順を保持する
//
// 備考: UIImageCollectSystem が UITransformComponent の computed 矩形と合わせて UIDrawList に積み、
//       UIRenderer が 2D パスで描く。texturePath が空なら color の単色で塗る（パネル・背景など）。
// ===========================================================================
#include <DirectXMath.h>
#include <string>

struct UIImageComponent
{
    std::string       texturePath;                      // 画像パス。空なら単色塗り
    DirectX::XMFLOAT4 color = {1.0f, 1.0f, 1.0f, 1.0f}; // 乗算カラー（単色塗り時はこの色そのもの）

    // 縦グラデーション。useGradient=true で color（上端）→ colorBottom（下端）に補間する
    bool              useGradient = false;
    DirectX::XMFLOAT4 colorBottom = {1.0f, 1.0f, 1.0f, 1.0f};

    bool additive  = false; // 加算合成で描くか（発光表現）
    bool visible   = true; // 描画するか
    int  sortOrder = 0;    // 描画順（大きいほど手前）
};
