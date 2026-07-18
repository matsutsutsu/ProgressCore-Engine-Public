#pragma once
/// @file UIDrawList.h
/// @brief 2Dパスで描画するUI要素のフラットなリスト（Worldリソース）。
///
/// 設計方針:
///   - UI描画は BaseScene::Render() の2Dパスで行う（renderステージSystemは UpdateRender で
///     動くため、そこで直接描画すると後続の3D描画に上書きされる）。
///   - 収集System群(UILayoutSystem=画像 / UITextCollectSystem=テキスト)が UpdateRender 中に
///     アイテムを積み、UIRenderer が2Dパスで sortOrder 順に消費する（既存 RenderQueue 流儀）。
///   - 画像/テキストを1つの配列で持ち sortOrder で層を統一する。クリアは消費側(Flush末尾)が行う
///     ため、収集System間の実行順に依存しない。
#include <DirectXMath.h>
#include <string>
#include <vector>

enum class UIDrawKind
{
    Image, ///< 画像 or 単色矩形
    Text,  ///< 文字列
};

struct UIDrawItem
{
    UIDrawKind        kind      = UIDrawKind::Image;
    DirectX::XMFLOAT2 pos       = {0.0f, 0.0f};       ///< 画面左上(px)
    DirectX::XMFLOAT2 size      = {0.0f, 0.0f};       ///< 矩形サイズ(px)。Textでは未使用。
    DirectX::XMFLOAT4 color     = {1.0f, 1.0f, 1.0f, 1.0f};
    float             rotation  = 0.0f;               ///< 度（Imageのみ）
    int               sortOrder = 0;                  ///< 大きいほど手前

    // Image 用
    std::string texturePath;                          ///< 空なら単色塗り

    // Text 用
    std::string text;                                 ///< 描画文字列
    float       pixelSize = 24.0f;                    ///< 文字の高さ(px)
};

struct UIDrawList
{
    std::vector<UIDrawItem> items;
    void Clear() { items.clear(); }
};
