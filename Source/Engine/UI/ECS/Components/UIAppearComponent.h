#pragma once
// ===========================================================================
// 概要: UI の出現アニメ（フェードイン + 下からのスライド）を 1 回だけ再生するコンポーネント UIAppearComponent。
//
// 責務:
//   - 出現アニメの設定（遅延・時間・スライド量）と進行状態を保持する
//
// 備考:
//   - リザルトやポーズなどのオーバーレイで、要素ごとに delay をずらして順番に出すために使う。
//   - 動かすのは UITransform.alpha と offset.y だけで、scale には触らない。
//     そのため UIButtonSystem の色の上書きや UIAnimationComponent の hover 拡大とぶつからない
//     （alpha は色の α に掛ける値なので、ボタン色が毎フレーム上書きされてもフェードが効く）。
//   - 既定は play=false で何もしない。生成側が play=true にした要素だけ再生する。
//     再生する要素は生成時に UITransform.alpha=0 にしておくこと（開始前に見えないように）。
// ===========================================================================
struct UIAppearComponent
{
    bool  play     = false; // 出現アニメを再生するか
    float delay    = 0.0f;  // 再生開始までの待ち時間（秒）。要素ごとにずらして順番に出す
    float duration = 0.28f; // フェードとスライドにかける時間（秒）
    float slideY   = 28.0f; // 開始時の下方向オフセット（px）。0 ならフェードだけ

    // UIAppearSystem が書く進行状態
    float elapsed     = 0.0f;  // 経過時間（秒）
    bool  captured    = false; // 基準の offset.y を取得済みか
    bool  done        = false; // 完了済みか（完了後は何もしない）
    float baseOffsetY = 0.0f;  // 演出前の offset.y（着地位置）
};
