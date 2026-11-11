#pragma once
#include <DirectXMath.h>
#include <string>

// UI の文字列の内容・大きさ・色。位置は UITransformComponent の computedPos（左上）から描く
// UITextCollectSystem → UIRenderer → TextRenderer（stb_truetype）の順に渡る
struct UITextComponent
{
    std::string       text      = "Text";                   // 描く文字列（ASCII のみ描画できる）
    float             pixelSize = 24.0f;                    // 文字の高さ（px）
    DirectX::XMFLOAT4 color     = {1.0f, 1.0f, 1.0f, 1.0f}; // 文字色
    bool              visible   = true;                     // 描画するか
    int               sortOrder = 10;                       // 大きいほど手前（既定で画像より上）

    // 文字の右下に暗い影を重ねるか。HUD は明るい床や空の上にも乗るので、白い文字は影がないと背景に溶ける
    bool shadow = false;
};
