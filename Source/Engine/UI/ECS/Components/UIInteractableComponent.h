#pragma once
// ===========================================================================
// 概要: UI 要素をマウス・パッド・キーで操作できるようにするコンポーネント UIInteractableComponent。
//
// 責務:
//   - 操作の設定（有効 / フォーカス対象 / 重なり時の優先度）を保持する
//   - UIInputSystem が書く hover / focus / press / click の状態を保持する
//
// 備考: 見た目（ボタン色など）は UIButtonSystem がこの状態を読んで反映する。
// ===========================================================================
struct UIInteractableComponent
{
    // 設定（保存対象）
    bool interactable = true; // false なら判定しない（Disabled 相当）
    bool focusable    = true; // パッド/キーのフォーカス移動対象にするか
    int  priority     = 0;    // ヒットテストの手前優先度（大きいほど上を拾う）

    // 以下は UIInputSystem が書く状態（保存しない）
    bool isHovered = false; // マウスカーソルが乗っている（最前面のみ true）
    bool isFocused = false; // パッド/キーで選択中（同時に1つ）
    bool isPressed = false; // この要素上で押下中
    bool clicked   = false; // このフレームでクリック成立（マウス離し or 決定ボタン）
};
