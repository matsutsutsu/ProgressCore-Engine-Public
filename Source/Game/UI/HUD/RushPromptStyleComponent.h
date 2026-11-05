#pragma once
// ===========================================================================
// 概要: ラッシュの「今押せ」の合図（攻撃ボタンのアイコンと、残り時間のリング）の見た目と動きの調整値。
//
// 責務:
//   - アイコンとリングの大きさ・色・出す位置を持つ
//   - 出る／消える動きと、残りわずかの警告を持つ
//
// 備考: 合図は UIDrawList へ直接積むので、調整値だけを PlayerHud プレハブの子 RushPrompt に置く。
//       同じ子に UIImage と InputIcon も付け、ボタンの画像は InputIconSystem に決めさせる。
// ===========================================================================
#include <DirectXMath.h>

struct RushPromptStyleComponent
{
    // --- 形 ---
    float             iconSize      = 48.0f;                       // ボタンのアイコンの一辺（px）。画像は 64px なので、これより大きくすると荒れる
    float             ringRadius    = 34.0f;                       // リングの半径（px）
    float             ringThickness = 3.0f;                        // リングの太さ（px）
    DirectX::XMFLOAT4 ringColor     = {1.0f, 0.92f, 0.70f, 1.0f};  // 残り時間の色
    DirectX::XMFLOAT4 trackColor    = {0.0f, 0.0f, 0.0f, 0.45f};   // 減った後に残る溝の色
    DirectX::XMFLOAT4 warnColor     = {0.95f, 0.18f, 0.22f, 1.0f}; // 残りわずかの色。ロックオンの照準と同じ深紅

    // --- 位置 ---
    DirectX::XMFLOAT2 targetOffset   = {72.0f, -48.0f}; // ロックオン中の敵の体の中心からずらす量（px）。照準と重ねない
    DirectX::XMFLOAT2 fallbackAnchor = {0.5f, 0.62f};   // ロックオンしていないときの位置（画面に対する割合）

    // --- 動き ---
    float appearTime = 0.15f; // 出た瞬間の大きさから、元の大きさに縮みきるまで（秒）
    float appearOpen = 0.6f;  // 出た瞬間の大きさ（1 + この値 倍から縮む）
    float warnRatio  = 0.3f;  // 残りがこの割合を切ったら、警告の色へ寄せる
    float burstTime  = 0.18f; // 押せたときに、広がりながら消えきるまで（秒）
    float burstScale = 0.8f;  // 押せたときに広がる量（1 + この値 倍まで）
    float fadeIn     = 20.0f; // 出る速さ（1/秒）
    float fadeOut    = 8.0f;  // 押さずに終わったとき、消える速さ（1/秒）
};
