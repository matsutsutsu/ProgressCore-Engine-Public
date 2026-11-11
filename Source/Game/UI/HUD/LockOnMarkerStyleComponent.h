#pragma once
#include <DirectXMath.h>

// ロックオンの照準（2本の弧の輪と中心の◆）の見た目と動きの調整値
// 照準は UI のエンティティを持たないので、PlayerHud プレハブの子 LockOnMarker に置く
struct LockOnMarkerStyleComponent
{
    // --- 形 ---
    DirectX::XMFLOAT4 color     = {0.95f, 0.18f, 0.22f, 1.0f}; // 照準の色。HUD の敵側と同じ深紅
    float             radius    = 16.0f;  // 輪の半径（px）。ボスの姿を隠さない小ささにする
    float             thickness = 2.0f;   // 弧の太さ（px）
    float             coreSize  = 5.0f;   // 中心の◆の一辺（px）
    float             arcSpan   = 130.0f; // 1本の弧の広がり（度）。残りの (180 - これ) 度ずつが切れ目

    // --- ラッシュの範囲 ---
    // 狙っている敵がラッシュを始められる距離（PlayerRushComponent.startMaxDistance）より遠いときの色
    // 今ジャスト回避しても反撃に移れないことを、照準の深紅から鮮やかさを抜いた色で伝える
    // 黄色系は HUD で「今がチャンス」（ラッシュの合図・チュートリアルの「今だ」）に使っているので避ける
    DirectX::XMFLOAT4 outOfRangeColor = {0.55f, 0.20f, 0.22f, 0.70f};
    float             rangeBlendSpeed = 10.0f; // 範囲の内外が変わったとき、色が移る速さ（1/秒）。境目でチカチカさせない

    // --- 動き ---
    float spinSpeed   = 1.2f;  // 弧の回る速さ（ラジアン/秒）
    float acquireTime = 0.2f;  // ロックした瞬間から、輪が縮みきるまでの時間（秒）
    float acquireOpen = 1.5f;  // 縮む前に開いている量（1 + この値 倍の大きさから縮む）
    float fadeIn      = 14.0f; // 出る速さ（1/秒）
    float fadeOut     = 7.0f;  // 消える速さ（1/秒）
};
