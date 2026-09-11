#pragma once
// UIの出現アニメ（1回きり）。フェードイン＋軽い上スライドで「スッと現れる」演出を付ける。
//
// 設計方針:
//   - リザルト/ポーズ等のオーバーレイが一斉にパッと出るのを避け、要素ごとに少し遅延させて
//     気持ちよく波及させる（delay）。
//   - 動かすのは UITransform.alpha（不透明度）と offset.y（位置）だけ。scale には触らないため、
//     UIButtonSystem の色毎フレーム上書きや UIAnimationComponent の hover 拡大と衝突しない。
//     （alpha は色αへの乗算チャンネルなので、ボタン色が上書きされてもフェードが効く。）
//   - play=false が既定＝何もしない。生成側（ファクトリ）が play=true にした要素だけ再生する。
//     再生時は生成時に UITransform.alpha=0 にしておくこと（開始前に見えないように）。
struct UIAppearComponent
{
    // --- 設定 ---
    bool  play     = false;  // 出現アニメを再生するか（false=無効・既定）
    float delay    = 0.0f;   // 再生開始までの待ち(秒)。要素ごとにずらして波及感を出す。
    float duration = 0.28f;  // フェード/スライドにかける時間(秒)
    float slideY   = 28.0f;  // 開始時の下方向オフセット(px)。0でスライド無し（フェードのみ）。

    // --- 内部状態（システムが更新） ---
    float elapsed     = 0.0f;   // 経過時間(秒)
    bool  captured    = false;  // 基準offsetを取得済みか
    bool  done        = false;  // 完了済みか（完了後は何もしない）
    float baseOffsetY = 0.0f;   // 演出前の offset.y（着地・復元用）
};
