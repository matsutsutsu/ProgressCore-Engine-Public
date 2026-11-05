#pragma once
// UI の出現アニメ（フェードイン + 下からのスライド）を1回だけ再生する。オーバーレイで要素ごとに順番に出す
// 動かすのは alpha と offset.y だけなので、ボタンの色の上書きや hover の拡大とぶつからない
// 既定は play が false。再生する要素は生成時に alpha を 0 にしておく
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
