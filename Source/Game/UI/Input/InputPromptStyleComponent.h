#pragma once
// ===========================================================================
// 概要: チュートリアルの画面右上に出す操作の説明（アイコンと行名）の並べ方の調整値。
//
// 責務:
//   - アイコン・行・行名の大きさと間隔を持つ
//   - 画面の右上からの余白と、全体の不透明度を持つ
//
// 備考: InputPrompt プレハブに付ける。出す行と文言は Assets/Config/InputPromptConfig.json が持つ。
// ===========================================================================

struct InputPromptStyleComponent
{
    // --- 大きさ（px） ---
    float iconSize   = 59.0f;  // アイコンの一辺
    float rowHeight  = 59.0f;  // 行の高さ。iconSize より小さいと行が重なる
    float labelWidth = 108.0f; // 行名の幅
    float labelGap   = 10.0f;  // アイコンと行名の間
    float textSize   = 28.0f;  // 行名の文字の高さ

    // --- 配置 ---
    float marginRight = 28.0f; // 画面の右端からの余白（px）
    float marginTop   = 12.0f; // 画面の上端からの余白（px）
    float alpha       = 0.7f;  // 不透明度。いつも出ているので控えめにする
};
