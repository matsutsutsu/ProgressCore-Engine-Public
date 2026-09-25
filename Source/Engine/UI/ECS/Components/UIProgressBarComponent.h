#pragma once
// ===========================================================================
// 概要: 横方向に伸び縮みするゲージ（HP など）のコンポーネント UIProgressBarComponent。
//
// 責務:
//   - 充填率と色、遅延バー・グラデーション・被弾の揺れの設定を保持する
//
// 備考:
//   - UIProgressBarCollectSystem が UITransform の矩形から「背景 + フィル（幅 × value）」を
//     単色の Image として UIDrawList に積む。
//   - 値は別の System（例: Game 側の CleaveGaugeHudSystem）が毎フレーム書き込む。
//   - showTrail=true で遅延ダメージバーを追加できる。背景とフィルの間に幅 × trailValue の帯を描き、
//     被弾で value が減った後に trailValue を遅れて追従させると、失った分が一瞬色付きで残る。
//     trailValue を動かすのも値を書き込む側の System。
// ===========================================================================
#include <DirectXMath.h>

struct UIProgressBarComponent
{
    float             value     = 1.0f;                       // 充填率 [0,1]
    DirectX::XMFLOAT4 bgColor   = {0.0f, 0.0f, 0.0f, 0.63f};  // 背景色
    DirectX::XMFLOAT4 fillColor = {0.27f, 0.67f, 1.0f, 1.0f}; // フィル色
    bool              visible   = true;
    int               sortOrder = 0; // 背景の描画順。遅延バーは +1、フィルは +2 で描く（大きいほど手前）

    // 遅延ダメージバー（HP ゲージ等で使う）
    bool              showTrail  = false;                       // 遅延バーを描くか
    float             trailValue = 1.0f;                        // 遅延バーの充填率 [0,1]（値を書く側が遅れて追従させる）
    DirectX::XMFLOAT4 trailColor = {1.0f, 0.85f, 0.85f, 0.95f}; // 遅延バーの色（失った分。淡い赤）

    // 以下は見た目だけの設定（値の意味は変えない）

    // フィルの下端をどれだけ暗くするか [0,1]。0 ならフラット。
    //   2 色ではなく「暗くする率」で持つのは、値を書く側が fillColor を毎フレーム差し替える
    //   （HP 残量で緑→黄→赤）ため。率なら色が変わってもグラデーションが追従する
    float gradient = 0.0f;

    // 被弾の揺れの強さ [0,1]。ゲージ全体（背景・遅延バー・フィル）をまとめて揺らす。
    //   減衰と位相の更新は値を書く側の System（HUD）が行い、ここは今の強さだけを持つ。
    //   収集 System は描画ステージで走るので、そこで時間を進めるとポーズ中も揺れ続けてしまう
    float shake        = 0.0f;
    float shakeElapsed = 0.0f; // 揺れの位相（秒）。値を書く側が加算する

    // 揺れの最大振幅（px）と振動数（Hz）。
    //   実際の振幅は shake の 2 乗に比例させる。線形だと減衰の終わりが長く続いて
    //   小刻みな揺れが残って見え、当たった瞬間の鋭さが出ない
    float shakeAmplitude = 5.0f;
    float shakeFrequency = 26.0f;
};
