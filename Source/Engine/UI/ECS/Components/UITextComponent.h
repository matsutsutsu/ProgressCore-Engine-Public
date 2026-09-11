#pragma once
// ECSネイティブUIの文字列描画データ。
//
// 設計方針:
//   - 「何の文字を・どの大きさ・色で」だけを保持。実描画は2DパスのTextRenderer(stb_truetype)。
//   - 配置は UITransformComponent（アンカー方式）に従い、computedPos 左上から描く。
#include <DirectXMath.h>
#include <string>

struct UITextComponent
{
    std::string       text      = "Text";                    // 描画文字列（現状ASCII）
    float             pixelSize = 24.0f;                      // 文字の高さ(px)
    DirectX::XMFLOAT4 color     = {1.0f, 1.0f, 1.0f, 1.0f};   // 文字色
    bool              visible   = true;                       // 描画するか
    int               sortOrder = 10;                         // 大きいほど手前（既定で画像より上）
};
