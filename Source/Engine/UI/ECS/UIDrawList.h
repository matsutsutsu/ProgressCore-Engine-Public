#pragma once
// ===========================================================================
// 概要: 2D パスで描く UI 要素を並べた描画リスト（World リソース）。
//
// 責務:
//   - 収集 System（UIImageCollectSystem / UITextCollectSystem / UIProgressBarCollectSystem）が
//     積んだ描画アイテムを、UIRenderer が消費するまで保持する
//
// 備考:
//   - UI は BaseScene::Render() の 2D パスで描く。Render ステージの System は UpdateRender 中に
//     走るので、そこで直接描くと後の 3D 描画に上書きされる。
//   - 画像と文字を 1 つの配列で持ち、sortOrder で前後を統一する。
//   - クリアは消費側（UIRenderer::Flush の末尾）が行う。
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
    UIDrawKind        kind      = UIDrawKind::Image;
    DirectX::XMFLOAT2 pos       = {0.0f, 0.0f}; // 画面上の左上（px）
    DirectX::XMFLOAT2 size      = {0.0f, 0.0f}; // 矩形のサイズ（px）。Text では使わない
    DirectX::XMFLOAT4 color     = {1.0f, 1.0f, 1.0f, 1.0f};
    float             rotation  = 0.0f; // 回転（度。Image のみ）
    int               sortOrder = 0;    // 大きいほど手前

    // 以下は Image 用
    std::string texturePath; // 空なら単色塗り

    // 下端の色。useGradient=true のときだけ使う（上端は color）。
    //   縦方向だけにしているのは、UI の矩形はそれで足り、4 隅を別々に持つ場面がほぼ無いため
    bool              useGradient = false;
    DirectX::XMFLOAT4 colorBottom = {1.0f, 1.0f, 1.0f, 1.0f};

    // 加算合成で描くか（発光表現）。ゲージ先端の光やヒットフラッシュに使う。
    //   加算は下にある絵に足して初めて光って見えるため、UIRenderer は通常合成を全て描いた後に
    //   まとめて描く。つまり additive を付けると sortOrder に関係なく最前面側になる
    bool additive = false;

    // テクスチャの切り出し範囲（正規化 [0,1]。xy=左上 / zw=幅と高さ）。既定は全体。
    //   アトラスの 1 コマを指す用途や、画像を引き伸ばさずに切って減らすゲージに使う
    DirectX::XMFLOAT4 uvRect = {0.0f, 0.0f, 1.0f, 1.0f};

    // 以下は Gauge 用。pos / size はゲージの矩形（縁は含まない）、color.w は全体の不透明度
    GpuData::CbUIGaugeData gauge{};

    // 以下は Text 用
    std::string text;              // 描く文字列
    float       pixelSize = 24.0f; // 文字の高さ（px）
};

struct UIDrawList
{
    std::vector<UIDrawItem> items;
    void                    Clear() { items.clear(); }

    // 境目の sortOrder より奥と手前で、不透明度を別々に掛ける。Clear では戻さない。
    //   設定画面のように、下の画面の UI を薄くしながら上の画面を出す（重ねて入れ替える）ときに使う
    //   （各部品の visible や alpha は HUD などが毎フレーム書き戻すので、外から隠すのに使えない）
    int   splitSortOrder = INT_MIN; // 境目。これ以上が手前
    float belowAlpha     = 1.0f;    // 境目より奥に掛ける不透明度（0 なら描かない）
    float aboveAlpha     = 1.0f;    // 境目から手前に掛ける不透明度（0 なら描かない）
};
