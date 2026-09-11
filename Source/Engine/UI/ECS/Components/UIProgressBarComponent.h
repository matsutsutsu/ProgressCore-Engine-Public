#pragma once
// 汎用ゲージ（背景＋横方向フィル）。HUDのHP/ゲージ等に使う再利用ウィジェット。
//
// 設計方針:
//   - value(0..1) と2色だけを持つデータ。UIProgressBarCollectSystem が UITransform の矩形から
//     背景矩形＋幅×value のフィル矩形の2枚を UIDrawList へ積む（描画は既存の単色Image経路）。
//   - 値の供給（データ連動）は別System（例: Game側 CleaveGaugeHudSystem）が担う（表現と供給の分離）。
//   - showTrail=true で「遅延ダメージ（チップ）バー」を1枚追加できる。背景とフィルの間に
//     幅×trailValue の帯を描き、被弾で value が即減った後 trailValue を遅れて追従させると、
//     失った分（value..trailValue）が一瞬色付きで残る。trailValue の駆動は供給側Systemが行う。
#include <DirectXMath.h>

struct UIProgressBarComponent
{
    float             value     = 1.0f;                     // 充填率 0..1
    DirectX::XMFLOAT4 bgColor   = {0.0f, 0.0f, 0.0f, 0.63f}; // 背景色
    DirectX::XMFLOAT4 fillColor = {0.27f, 0.67f, 1.0f, 1.0f}; // フィル色
    bool              visible   = true;
    int               sortOrder = 0; // 背景=これ / フィル=これ(+1 or +2)（大きいほど手前）

    // --- 遅延ダメージ（チップ）バー（既定OFF。HPゲージ等で使う） ---
    bool              showTrail  = false;                     // 遅延バーを描くか
    float             trailValue = 1.0f;                      // 遅延バーの充填率 0..1（供給側が遅れて追従）
    DirectX::XMFLOAT4 trailColor = {1.0f, 0.85f, 0.85f, 0.95f}; // 遅延バーの色（失った分＝淡い白赤）

    // --- 質感（見た目だけ。値の意味は変えない） ---

    // 下端をどれだけ暗くするか 0..1。0=フラット（既定＝従来どおり）。
    // 2色でなく「暗くする率」なのは、供給側が fillColor を毎フレーム差し替える
    // （HP残量で緑→黄→赤に変わる）ため。率なら色を変えてもグラデが自動で追従する。
    float gradient = 0.0f;

    // 被弾の揺れ 0..1。ゲージ全体（背景・遅延バー・フィル）をまとめて震わせる。
    //
    // 減衰と位相送りは供給側System（HUD）が行う。ウィジェットは「今どれだけ揺れるか」
    // だけを持つ。ここで自分で時間を進めると、収集Systemが描画ステージで走る都合上
    // ポーズ中も揺れ続けてしまう（HUD側の dt を通せば世界の時計に従う）。
    float shake        = 0.0f;
    float shakeElapsed = 0.0f; // 揺れの位相[秒]。供給側が加算する。

    // 揺れの最大振幅(px)と振動数(Hz)。
    //
    // 実際の振幅は shake ではなく shake*shake に比例させる。線形だと尾を引いて
    // 「ずっと小刻みに動いている」ように見え、当たった瞬間の鋭さが出ないため。
    float shakeAmplitude = 5.0f;
    float shakeFrequency = 26.0f;
};
