#pragma once
// ゲーム画面（RT）が実際に表示されている矩形（World リソース）。エディタの GameViewWindow が毎フレーム書く
// UIInputSystem は valid のときだけ、マウス座標をゲーム RT の解像度へ変換する。全画面では変換しない
// valid だけでは全画面とゲーム画面が隠れた状態を区別できないので、editorVisible で分ける
// 分けないと、ほかのウィンドウのクリックが裏のポーズメニューのボタンに当たる
// x,y,w,h はメインウィンドウのクライアント座標（px）。マルチビューポートでは原点を引いてから書く
struct GameViewportInfo
{
    float x     = 0.0f;  // ゲーム画像の左上 X（クライアント座標 px）
    float y     = 0.0f;  // ゲーム画像の左上 Y（クライアント座標 px）
    float w     = 0.0f;  // ゲーム画像の表示幅（レターボックス後の実サイズ px）
    float h     = 0.0f;  // ゲーム画像の表示高さ（レターボックス後の実サイズ px）
    bool  valid = false; // エディタの GameView に RT が表示されているフレームだけ true

    // エディタ UI が表示されているフレームだけ true（全画面プレイ / Shipping では false）
    //   editorVisible && !valid は「エディタは動いているがゲーム画面は出ていない」状態で、
    //   このときゲーム UI は入力を受け取ってはいけない
    bool editorVisible = false;

    // エディタ表示中に、キーボードをゲームへ渡してよいフレームだけ true
    //   GameView にフォーカスがあり、かつ UI 操作モード（F2）でないときに限る
    //   これがないと、BT エディタで押した Space（ノード追加）や Esc が、裏のポーズメニューの
    //   決定・解除としても効いてしまう（生のキー入力を読むので ImGui のゲートを通らない）
    bool keyboardFocus = false;

    // ゲーム UI（ポーズメニューなど）がキーボードを受け付けてよいか。全画面 / Shipping では常に true
    bool KeyboardToGame() const { return !editorVisible || keyboardFocus; }
};
