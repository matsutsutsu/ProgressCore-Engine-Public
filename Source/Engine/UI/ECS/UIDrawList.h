#pragma once
// 2Dパスで描画するUI要素のフラットなリスト（Worldリソース）。
//
// 設計方針:
//   - UI描画は BaseScene::Render() の2Dパスで行う（renderステージSystemは UpdateRender で
//     動くため、そこで直接描画すると後続の3D描画に上書きされる）。
//   - 収集System群(UILayoutSystem=画像 / UITextCollectSystem=テキスト)が UpdateRender 中に
//     アイテムを積み、UIRenderer が2Dパスで sortOrder 順に消費する（既存 RenderQueue 流儀）。
//   - 画像/テキストを1つの配列で持ち sortOrder で層を統一する。クリアは消費側(Flush末尾)が行う
//     ため、収集System間の実行順に依存しない。
#include <DirectXMath.h>
#include <string>
#include <vector>

enum class UIDrawKind
{
    Image, // 画像 or 単色矩形
    Text,  // 文字列
};

struct UIDrawItem
{
    UIDrawKind        kind      = UIDrawKind::Image;
    DirectX::XMFLOAT2 pos       = {0.0f, 0.0f}; // 画面左上(px)
    DirectX::XMFLOAT2 size      = {0.0f, 0.0f}; // 矩形サイズ(px)。Textでは未使用。
    DirectX::XMFLOAT4 color     = {1.0f, 1.0f, 1.0f, 1.0f};
    float             rotation  = 0.0f; // 度（Imageのみ）
    int               sortOrder = 0;    // 大きいほど手前

    // --- Image 用 ---
    std::string texturePath; // 空なら単色塗り

    // 下端の色。useGradient=true のときだけ使う（上端は color）。
    // なぜ2色だけか: UIの矩形は縦方向のグラデがあれば大半の質感が出る。
    // 4隅を個別に持たせるとデータが倍になる割に、使う場面がほぼ無い。
    bool              useGradient = false;
    DirectX::XMFLOAT4 colorBottom = {1.0f, 1.0f, 1.0f, 1.0f};

    // 加算合成で描くか（発光）。ゲージ先端の光やヒットフラッシュに使う。
    //
    // 加算は「下に積まれた絵の上」で足されて初めて光って見えるため、
    // UIRenderer は通常合成を全部描いた後にまとめて描く。
    // つまり additive を付けた時点で sortOrder より後ろには回らない。
    bool additive = false;

    // テクスチャの切り出し範囲（正規化 0..1。xy=左上 / zw=幅,高さ）。既定は全体。
    // アトラスの1コマを指す用途と、「引き伸ばさずに切って減らすゲージ」に使う。
    DirectX::XMFLOAT4 uvRect = {0.0f, 0.0f, 1.0f, 1.0f};

    // Text 用
    std::string text;              // 描画文字列
    float       pixelSize = 24.0f; // 文字の高さ(px)
};

struct UIDrawList
{
    std::vector<UIDrawItem> items;
    void                    Clear() { items.clear(); }
};
