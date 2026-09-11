#pragma once
// ボタンの状態別見た目とクリック時アクション。
//
// 設計方針:
//   - UIInteractableComponent の hover/press/click 状態に応じ、UIButtonSystem が
//     UIImageComponent.color を状態色へ差し替える（Slice 3 は色遷移。スプライト遷移は後続）。
//   - clicked 時に onClickAction（文字列）のハッシュを UIButtonClickedEvent で発火。ゲーム側が解釈。
#include <DirectXMath.h>
#include <string>

struct UIButtonComponent
{
    DirectX::XMFLOAT4 normalColor   = {0.25f, 0.28f, 0.35f, 1.0f};
    DirectX::XMFLOAT4 hoverColor    = {0.35f, 0.42f, 0.60f, 1.0f};
    DirectX::XMFLOAT4 pressedColor  = {0.15f, 0.18f, 0.26f, 1.0f};
    DirectX::XMFLOAT4 disabledColor = {0.20f, 0.20f, 0.22f, 0.5f};

    float hoverScale = 1.08f; // hover/focus 時の拡大率（UIAnimationComponent があれば追従演出）

    std::string onClickAction; // クリック時に発火する識別名（空可）。ハッシュ化してイベントで通知。
};
