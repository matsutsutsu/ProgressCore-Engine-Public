#pragma once
// ===========================================================================
// 概要: チュートリアルのヒントの帯の並べ方（寸法・文字の大きさ）と、System が切り替える色・速さの調整値。
//
// 責務:
//   - 帯の高さ・左右の余白・アイコンの大きさ・部品の間隔・文字の大きさを持つ
//   - ゲージの色（進行中・達成）と、文字の色（通常・「今だ」）を持つ
//   - フェードとゲージの伸びる速さを持つ
//
// 備考: TutorialHint プレハブ（Assets/Prefabs/UI/TutorialHint.json）のルートに付ける。
//       帯の幅と部品の並びは、文の長さに合わせて TutorialHintHudSystem が毎回計算する。
//       そのため帯の部品の位置・大きさはプレハブでは決めず、ここの寸法から決まる。
//       帯全体の高さの位置は TutorialHintRow の UI Transform、帯の色は各部品の UI Image で調整する。
// ===========================================================================
#include <DirectXMath.h>

struct TutorialHintStyleComponent
{
    // --- 寸法（px） ---
    float bandHeight = 90.0f; // 帯の高さ
    float padX       = 39.0f; // 帯の左右の余白
    float iconSize   = 64.0f; // アイコンの一辺。画像と同じ 64 にしておく（拡大すると荒れる）
    float gap        = 22.0f; // アイコン・「+」・文の間
    float textSize   = 36.0f; // 文字の高さ
    float emphSize   = 48.0f; // 「今だ」の文字の高さ（文字は 48px で焼いてあるので、これより大きくすると荒れる）
    float barHeight  = 8.0f;  // 帯の真下のゲージの太さ

    // --- 色 ---
    DirectX::XMFLOAT4 fillColor  = {0.35f, 0.75f, 1.00f, 0.95f}; // ゲージ（進行中）
    DirectX::XMFLOAT4 clearColor = {0.40f, 0.95f, 0.50f, 0.95f}; // ゲージ（達成の余韻）
    DirectX::XMFLOAT4 textColor  = {1.00f, 1.00f, 1.00f, 1.00f}; // 説明の文字
    DirectX::XMFLOAT4 nowColor   = {1.00f, 0.85f, 0.30f, 1.00f}; // 「今だ」の文字

    // --- 速さ（大きいほど速い） ---
    float fadeSpeed    = 10.0f; // 帯のフェード
    float fillSpeed    = 12.0f; // ゲージの伸び
    float nowFadeSpeed = 20.0f; // 中央のボタンのフェード。押す瞬間なので速く
};
