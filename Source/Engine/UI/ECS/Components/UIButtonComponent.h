#pragma once
#include <DirectXMath.h>
#include <string>

// ボタンの状態ごとの色と、クリック時のアクション名。状態ごとの画像の切り替えはなく、色だけ
// UIButtonSystem が操作の状態を見て色を差し替え、クリックで UIButtonClickedEvent を出す
struct UIButtonComponent
{
    DirectX::XMFLOAT4 normalColor   = {0.25f, 0.28f, 0.35f, 1.0f};
    DirectX::XMFLOAT4 hoverColor    = {0.35f, 0.42f, 0.60f, 1.0f};
    DirectX::XMFLOAT4 pressedColor  = {0.15f, 0.18f, 0.26f, 1.0f};
    DirectX::XMFLOAT4 disabledColor = {0.20f, 0.20f, 0.22f, 0.5f};

    float hoverScale = 1.08f; // hover/focus 時の拡大率（UIAnimationComponent がある場合だけ効く）

    std::string onClickAction; // クリック時に通知するアクション名（空可）。ハッシュにしてイベントで送る
};
