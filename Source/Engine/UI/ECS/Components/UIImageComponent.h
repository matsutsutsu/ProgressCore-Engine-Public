#pragma once
// ECSネイティブUIの画像（または単色パネル）描画データ。
//
// 設計方針:
//   - このコンポーネントは「何を・どう描くか」のデータのみ保持。実描画は 2Dパスで
//     UIRenderSystem/UIRenderer が UITransformComponent の computed 矩形を使って行う。
//   - texturePath が空なら color による単色塗り（パネル/ゲージ背景など）に使える。
#include <DirectXMath.h>
#include <string>

struct UIImageComponent
{
    std::string       texturePath;                      // 画像パス。空なら単色塗り。
    DirectX::XMFLOAT4 color = {1.0f, 1.0f, 1.0f, 1.0f}; // 乗算カラー（単色塗り時はこの色そのもの）

    // 縦グラデーション。useGradient=true で color(上端)→colorBottom(下端) に補間する。
    // 単色ベタのパネルは「作りかけ」に見えやすいので、暗転や枠はこれを使うと質感が出る。
    bool              useGradient = false;
    DirectX::XMFLOAT4 colorBottom = {1.0f, 1.0f, 1.0f, 1.0f};

    // 加算合成で描くか（発光）。
    bool additive  = false;
    bool visible   = true; // 描画するか
    int  sortOrder = 0;    // 描画順（大きいほど手前）
};
