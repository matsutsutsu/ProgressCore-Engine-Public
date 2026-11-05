#pragma once
// UI 要素をマウス・パッド・キーで操作できるようにする。hover / focus / press / click は UIInputSystem が書く
// 見た目への反映は、UIButtonSystem がこの状態を読んで行う
struct UIInteractableComponent
{
    // 設定（保存対象）
    bool interactable = true; // false なら判定しない（Disabled 相当）
    bool focusable    = true; // パッド/キーのフォーカス移動対象にするか
    int  priority     = 0;    // ヒットテストの手前優先度（大きいほど上を拾う）

    // UIInputSystem が書く状態
    bool isHovered = false; // マウスカーソルが乗っている（最前面のみ true）
    bool isFocused = false; // パッド/キーで選択中（同時に1つ）
    bool isPressed = false; // この要素上で押下中
    bool clicked   = false; // このフレームでクリック成立（マウス離し or 決定ボタン）
};
