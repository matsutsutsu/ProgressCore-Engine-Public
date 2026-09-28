#pragma once
// ===========================================================================
// 概要: ゲーム画面（RT）が実際に表示されている矩形をエディタからエンジンへ渡す World リソース。
//
// 責務:
//   - UIInputSystem がマウス座標を UI の座標系（ゲーム RT の解像度）へ変換するための矩形を持つ
//   - ゲーム UI が入力を受け付けてよいかを判定するための情報を持つ
//
// 備考:
//   - 全画面（F1）ではクライアント座標と RT が 1:1 で一致するが、エディタの GameView では
//     RT がレターボックス付きで縮小表示されるため、原点も倍率もずれる。
//   - エディタ（GameViewWindow）が毎フレーム表示矩形を書き込み、valid=true にする。
//     UIInputSystem は valid のときだけ (mouse - {x,y}) / {w,h} * screenW/H で変換する。
//     全画面 / Shipping では誰も書かないので valid=false のまま、変換なしで使う。
//     そのためエンジン側はエディタにも ImGui にも依存せず、#ifdef も要らない。
//   - valid=false だけでは「変換なしでよい（全画面）」と「ゲーム画面が出ていない
//     （エディタで GameView が隠れている）」を区別できない。後者で変換なしにすると、
//     画面に出ていないボタンの矩形に他のエディタウィンドウ上のクリックが当たる
//     （BehaviorTree ウィンドウのクリックが裏のポーズメニューの QUIT を押し、アプリが終了した）。
//     そのため editorVisible で区別する。
//   - x,y,w,h は Mouse::GetPositionX/Y と同じメインウィンドウのクライアント座標（px）。
//     ImGui のマルチビューポートではスクリーン座標が OS デスクトップ基準になるので、
//     GameViewWindow 側でメインビューポートの原点を引いてから書き込むこと。
// ===========================================================================
struct GameViewportInfo
{
    float x     = 0.0f;  // ゲーム画像の左上 X（クライアント座標 px）
    float y     = 0.0f;  // ゲーム画像の左上 Y（クライアント座標 px）
    float w     = 0.0f;  // ゲーム画像の表示幅（レターボックス後の実サイズ px）
    float h     = 0.0f;  // ゲーム画像の表示高さ（レターボックス後の実サイズ px）
    bool  valid = false; // エディタの GameView に RT が表示されているフレームだけ true

    // エディタ UI が表示されているフレームだけ true（全画面プレイ / Shipping では false）。
    //   editorVisible && !valid は「エディタは動いているがゲーム画面は出ていない」状態で、
    //   このときゲーム UI は入力を受け取ってはいけない
    bool editorVisible = false;

    // エディタ表示中に、キーボードをゲームへ渡してよいフレームだけ true。
    //   GameView にフォーカスがあり、かつ UI 操作モード（F2）でないときに限る。
    //   これが無いと、BT エディタで押した Space（ノード追加）や Esc が、裏のポーズメニューの
    //   決定・解除としても効いてしまう（生のキー入力を読むので ImGui のゲートを通らない）。
    bool keyboardFocus = false;

    // ゲーム UI（ポーズメニューなど）がキーボードを受け付けてよいか。全画面 / Shipping では常に true
    bool KeyboardToGame() const { return !editorVisible || keyboardFocus; }
};
