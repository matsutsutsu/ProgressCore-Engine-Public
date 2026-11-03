#pragma once
// ===========================================================================
// 概要: ロックオンの照準（2本の弧の輪と中心の◆）の見た目と動きの調整値。
//
// 責務:
//   - 照準の色・輪の半径・線の太さ・◆の大きさ・弧の広がりを持つ
//   - 弧の回る速さ、ロックした瞬間に輪が縮む時間と開き具合を持つ
//   - 出る・消える速さを持つ
//
// 備考: 照準は UI のエンティティを持たず、LockOnMarkerHudSystem が毎フレーム線を直接描く。
//       そのため調整値だけを、PlayerHud プレハブ（Assets/Prefabs/UI/PlayerHud.json）の
//       子 LockOnMarker に付けて持つ。見つからないシーンでは既定値で描く。
// ===========================================================================
#include <DirectXMath.h>

struct LockOnMarkerStyleComponent
{
    // --- 形 ---
    DirectX::XMFLOAT4 color     = {0.95f, 0.18f, 0.22f, 1.0f}; // 照準の色。HUD の敵側と同じ深紅
    float             radius    = 16.0f;  // 輪の半径（px）。ボスの姿を隠さない小ささにする
    float             thickness = 2.0f;   // 弧の太さ（px）
    float             coreSize  = 5.0f;   // 中心の◆の一辺（px）
    float             arcSpan   = 130.0f; // 1本の弧の広がり（度）。残りの (180 - これ) 度ずつが切れ目

    // --- 動き ---
    float spinSpeed   = 1.2f;  // 弧の回る速さ（ラジアン/秒）
    float acquireTime = 0.2f;  // ロックした瞬間から、輪が縮みきるまでの時間（秒）
    float acquireOpen = 1.5f;  // 縮む前に開いている量（1 + この値 倍の大きさから縮む）
    float fadeIn      = 14.0f; // 出る速さ（1/秒）
    float fadeOut     = 7.0f;  // 消える速さ（1/秒）
};
