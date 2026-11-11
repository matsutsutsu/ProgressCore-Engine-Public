#pragma once
#include <DirectXMath.h>

// 横に伸び縮みするゲージ（HP など）。値は別の System（PlayerHealthHudSystem など）が毎フレーム書く
// UIRenderer が UIGaugePS で四角形1枚に描くので、傾いた形や区切りも継ぎ目なしに描ける
// 時間で進む演出は、HUD が書く hitAge / healAge で決める。描画ステージで進めるとポーズ中も進む
struct UIProgressBarComponent
{
    float             value     = 1.0f;                       // 充填率 [0,1]
    DirectX::XMFLOAT4 bgColor   = {0.0f, 0.0f, 0.0f, 0.63f};  // 背景色
    DirectX::XMFLOAT4 fillColor = {0.27f, 0.67f, 1.0f, 1.0f}; // フィル色
    bool              visible   = true;
    int               sortOrder = 0; // 描画順（大きいほど手前）

    // 遅延ダメージバー（HP ゲージ等で使う）
    bool              showTrail  = false;                       // 遅延バーを描くか
    float             trailValue = 1.0f;                        // 遅延バーの充填率 [0,1]（値を書く側が遅れて追従させる）
    DirectX::XMFLOAT4 trailColor = {1.0f, 0.85f, 0.85f, 0.95f}; // 遅延バーの色（失った分。淡い赤）

    // 以下は見た目だけの設定（値の意味は変えない）

    // フィルの下端をどれだけ暗くするか [0,1]。0 ならフラット
    //   2 色ではなく「暗くする率」で持つのは、値を書く側が fillColor を毎フレーム差し替える
    //   （HP が少ないと赤くする）ため。率なら色が変わってもグラデーションが追従する
    float gradient = 0.0f;

    // ゲージの外側を囲む縁の太さ（px）。0 なら描かない
    //   明るい背景（床・空）の上では、淡い色のフィルの輪郭が背景に溶けて長さが読めなくなる
    //   暗い縁で囲むと、背景が何色でもゲージの範囲が分かる
    float             frame      = 0.0f;
    DirectX::XMFLOAT4 frameColor = {0.0f, 0.0f, 0.0f, 0.6f}; // 縁の色

    // 上辺を右へずらす量（px）。0 なら長方形、正なら「/」に傾いた平行四辺形になる
    //   幅は矩形のまま変えず、両端を斜めに切る
    float slant = 0.0f;

    // 区切りの数と隙間の幅（px）。segments が 1 以下なら区切らない
    //   区切りがあると「あと何割」を目で数えられる
    float segments   = 0.0f;
    float segmentGap = 2.0f;

    // フィル先端の光。減っている途中だけ出る（満タンと空では先端がない）
    float             glowWidth = 6.0f;                       // 光の広がり（px）
    DirectX::XMFLOAT4 glowColor = {1.0f, 1.0f, 1.0f, 0.0f};   // rgb = 色、a = 強さ（0 なら光らせない）

    // フィルの上を流れる光沢の強さ。0 なら流さない
    float sheen = 0.0f;

    // 目印の線を引く割合 [0,1]。負なら引かない。ボスの形態が変わる HP など「ここで何か起きる」位置に使う
    float marker      = -1.0f;
    float markerWidth = 2.0f; // 目印の線の太さ（px）

    // 光沢が流れる位相（秒）。値を書く側が加算する（shakeElapsed と同じく、ポーズ中は止める）
    float animTime = 0.0f;

    // 被弾の揺れの強さ [0,1]。ゲージ全体をまとめて揺らし、同じ強さでフィルを白く飛ばす
    //   減衰と位相の更新は値を書く側の System（HUD）が行い、ここは今の強さだけを持つ
    //   収集 System は描画ステージで走るので、そこで時間を進めるとポーズ中も揺れ続けてしまう
    float shake        = 0.0f;
    float shakeElapsed = 0.0f; // 揺れの位相（秒）。値を書く側が加算する

    // 揺れの最大振幅（px）と振動数（Hz）
    //   実際の振幅は shake の 2 乗に比例させる。線形だと減衰の終わりが長く続いて
    //   小刻みな揺れが残って見え、当たった瞬間の鋭さが出ない
    float shakeAmplitude = 5.0f;
    float shakeFrequency = 26.0f;

    // 被弾したときにフィルを白く飛ばす最大量 [0,1]（shake の 2 乗を掛けて使う）
    float hitFlash = 0.5f;

    // ------------------------------------------------------------
    // 演出の強さ（設定。HUD は Initialize で決め、エディタからも変えられる）
    // ------------------------------------------------------------
    float glitch = 0.0f; // 被弾したとき、横に裂けて色が分かれるグリッチの強さ [0,1]。0 なら出さない

    float             auraWidth = 8.0f;                       // 外側の光の広がり（px）
    DirectX::XMFLOAT4 auraColor = {1.0f, 0.2f, 0.15f, 0.0f}; // 外側の光（rgb = 色、a = 最大の強さ。auraLevel を掛けて使う）

    // ------------------------------------------------------------
    // 演出の進み具合（HUD が毎フレーム書く。保存しない）
    // ------------------------------------------------------------
    float hitAge    = 1000.0f; // 最後に被弾してからの秒数（大きい値なら演出は終わっている）
    float healFrom  = 0.0f;    // 最後に回復する前の割合（光が満ちていく範囲は healFrom〜value）
    float healAge   = 1000.0f; // 最後に回復してからの秒数
    float auraLevel = 0.0f;    // 外側の光の今の強さ [0,1]。auraColor.a に掛ける
};
