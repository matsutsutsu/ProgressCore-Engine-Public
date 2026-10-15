#pragma once
// ===========================================================================
// 概要: 2D パスで描く UI 要素を並べた描画リスト（World リソース）。
//
// 責務:
//   - 収集 System（UIImageCollectSystem / UITextCollectSystem / UIProgressBarCollectSystem など）が
//     積んだ描画アイテムを、UIRenderer が描き終えるまで保持する
//   - 画面全体に掛ける不透明度（重ねて入れ替える途中・ムービー中の HUD）を保持する
//
// 備考:
//   UI は BaseScene::Render() の 2D パスで描く。
//   描画ステージの System は UpdateRender 中に走るので、そこで直接描くと、後の 3D 描画に上書きされる。
//
//   画像・文字・ゲージを1つの配列で持ち、sortOrder で前後を決める。
//
//   items のクリアは、描く側（UIRenderer::Flush の末尾）が行う。
// ===========================================================================
#include "Engine/Graphics/Shader/Shared/UIShared.h" // ゲージの値（シェーダーへそのまま渡す）

#include <DirectXMath.h>
#include <climits>
#include <string>
#include <vector>

enum class UIDrawKind
{
    Image, // 画像 or 単色矩形
    Text,  // 文字列
    Gauge, // ゲージ（UIGaugePS で1枚に描く）
};

struct UIDrawItem
{
    // --- 共通 ---
    UIDrawKind        kind      = UIDrawKind::Image;
    DirectX::XMFLOAT2 pos       = {0.0f, 0.0f};             // 画面上の左上（px）
    DirectX::XMFLOAT2 size      = {0.0f, 0.0f};             // 四角形の大きさ（px）。Text では使わない
    DirectX::XMFLOAT4 color     = {1.0f, 1.0f, 1.0f, 1.0f}; // 色。Gauge では w だけを全体の不透明度に使う
    float             rotation  = 0.0f;                     // 中心まわりの回転（度）。Image だけ
    int               sortOrder = 0;                        // 描画順。大きいほど手前

    // --- Image ---
    std::string       texturePath;                            // 空なら単色の四角形
    bool              useGradient = false;                    // 上端 color・下端 colorBottom の縦グラデーションにするか
    DirectX::XMFLOAT4 colorBottom = {1.0f, 1.0f, 1.0f, 1.0f}; // 下端の色。useGradient のときだけ使う
    bool              additive    = false;                    // 加算合成で描くか（発光）。描画順に関係なく通常合成の後に描く
    DirectX::XMFLOAT4 uvRect      = {0.0f, 0.0f, 1.0f, 1.0f}; // テクスチャの切り出し範囲（0〜1。xy = 左上、zw = 幅と高さ）

    // --- Gauge（pos / size はゲージの四角形。縁は含まない） ---
    GpuData::CbUIGaugeData gauge{}; // シェーダーへそのまま渡すゲージの値

    // --- Text ---
    std::string text;              // 描く文字列（UTF-8）
    float       pixelSize = 24.0f; // 文字の高さ（px）
};

struct UIDrawList
{
    std::vector<UIDrawItem> items;
    void                    Clear() { items.clear(); } // items だけを空にする（下の不透明度は戻さない）

    // --- 重ねて入れ替える途中の不透明度（設定画面が書く） ---
    int   splitSortOrder = INT_MIN; // 奥と手前の境目の描画順。これ以上が手前
    float belowAlpha     = 1.0f;    // 境目より奥に掛ける不透明度（0 なら描かない）
    float aboveAlpha     = 1.0f;    // 境目から手前に掛ける不透明度（0 なら描かない）

    // --- HUD の不透明度（ムービー中に CinematicFrameSystem が書く） ---
    int   hudSortEnd = 150;  // これより奥（描画順が小さい）を HUD とみなす
    float hudAlpha   = 1.0f; // HUD に掛ける不透明度（0 なら描かない）
};
