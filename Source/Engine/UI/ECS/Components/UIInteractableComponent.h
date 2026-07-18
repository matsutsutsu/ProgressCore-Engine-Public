#pragma once
/// @file UIInteractableComponent.h
/// @brief UI要素をマウス操作可能にする（ヒットテスト対象＋hover/press/click状態）。
///
/// 設計方針:
///   - UITransform の矩形に対して UIInputSystem がマウス判定を行い、状態フラグを書き込む。
///   - 見た目（ボタン色など）は別System(UIButtonSystem)がこの状態を読んで反映する（判定と表現の分離）。
struct UIInteractableComponent
{
    // --- 設定（シリアライズ対象） ---
    bool interactable = true; ///< false なら判定しない（Disabled 相当）
    int  priority     = 0;    ///< ヒットテストの手前優先度（大きいほど上を拾う）

    // --- 状態（UIInputSystem が書く・非シリアライズ） ---
    bool isHovered = false; ///< カーソルが乗っている（最前面のみ true）
    bool isPressed = false; ///< この要素上で押下中
    bool clicked   = false; ///< このフレームでクリック成立（押下→同要素で離す）
};
