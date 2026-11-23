#pragma once
#include <DirectXMath.h>

// メニュー画面とテロップに共通する見た目の調整値。MenuStyle プレハブの1体に付ける
// 画面は開くときにここを読むので、調整は開き直すと反映される
struct MenuStyleComponent
{
    // --- 見出しと記録の行（画面の左上からの px） ---
    float left        = 120.0f; // 見出し・線・行の左端
    float headY       = 96.0f;  // 見出しの上端
    float headPx      = 88.0f;  // 見出しの文字の高さ
    float ruleGap     = 20.0f;  // 見出しの下端から線まで
    float ruleW       = 820.0f; // 線の長さ
    float subGap      = 16.0f;  // 線から、そのすぐ下の1行の上端まで
    float subPx       = 26.0f;  // 線のすぐ下の1行の文字の高さ
    float rowGap      = 72.0f;  // 線の下の1行から、記録の1行目の上端まで
    float rowH        = 56.0f;  // 記録の行の間隔
    float rowPx       = 32.0f;  // 記録の文字の高さ
    float valueOffset = 300.0f; // 左端から、記録の値の列まで
    float typeSpeed   = 70.0f;  // 1秒に打ち込む文字数

    // --- 色 ---
    DirectX::XMFLOAT4 gold  = {0.96f, 0.88f, 0.62f, 1.0f};  // 飾り・勝ち
    DirectX::XMFLOAT4 red   = {0.95f, 0.18f, 0.22f, 1.0f};  // ボス・負け
    DirectX::XMFLOAT4 label = {0.93f, 0.91f, 0.86f, 0.55f}; // 記録の項目名（沈める）
    DirectX::XMFLOAT4 value = {0.93f, 0.91f, 0.86f, 1.0f};  // 記録の値
    DirectX::XMFLOAT4 sub   = {0.93f, 0.91f, 0.86f, 0.8f};  // 線の下の1行

    // --- ボタン ---
    DirectX::XMFLOAT2 buttonSize  = {420.0f, 72.0f};            // 帯の大きさ（＝当たり判定）
    float             buttonPitch = 92.0f;                      // ボタンを縦に並べる間隔（中心から中心）。帯の高さより大きくする
    float             labelPad    = 46.0f;                      // 帯の左端からラベルの左端まで（斜めに切った分をよける）
    float             labelPx     = 34.0f;                      // ラベルの文字の高さ
    DirectX::XMFLOAT4 buttonBg    = {0.0f, 0.0f, 0.02f, 0.40f}; // 選んでいないときも見える暗い板
    DirectX::XMFLOAT4 buttonFill  = {0.96f, 0.88f, 0.62f, 1.0f}; // 選んだときに伸びる帯。HP ゲージと同じ白金
    DirectX::XMFLOAT4 buttonGlow  = {1.0f, 1.0f, 1.0f, 1.0f};   // 伸びている途中の帯の先端の光
    float             buttonSlant = 22.0f;                      // 帯の両端を斜めに切る幅（px）
    float             buttonSheen = 0.3f;                       // 選んでいる間に流れる光沢の強さ
    DirectX::XMFLOAT4 labelIdle   = {0.92f, 0.92f, 0.94f, 0.85f}; // ラベル（選んでいない）
    DirectX::XMFLOAT4 labelFocus  = {0.06f, 0.07f, 0.10f, 1.00f}; // ラベル（選んでいる）。白金の帯の上で読める暗い色
    float             focusSpeed  = 16.0f;                      // 帯が伸び縮みする速さ（16 で約 0.2 秒）
    float             pressTime   = 0.35f;                      // 押したときの白飛びと揺れが収まるまで（秒）

    // --- 操作のヒント ---
    float             hintIconPx = 40.0f;                        // アイコンの大きさ
    float             hintTextPx = 24.0f;                        // 文字の高さ
    float             hintGap    = 52.0f;                        // 一番下のボタンの下端から、ヒントの行の中心まで
    DirectX::XMFLOAT4 hintColor  = {0.78f, 0.80f, 0.86f, 1.0f}; // 文字の色。ボタンの文字より少し暗くして目立たせない

    // --- 上の値から求める位置（画面の左上からの px） ---
    float RuleY() const { return headY + headPx + ruleGap; } // 見出しの下の線
    float SubY() const { return RuleY() + subGap; }          // 線のすぐ下の1行の上端
    float RowY() const { return SubY() + rowGap; }           // 記録の1行目の上端
    float ValueX() const { return left + valueOffset; }      // 記録の値の列の左端

    // ヒントの行の縦の位置。一番下のボタンの中心の Y を渡す（ボタンと同じ基準の px）
    float HintY(float lastButtonY) const { return lastButtonY + buttonSize.y * 0.5f + hintGap; }
};
