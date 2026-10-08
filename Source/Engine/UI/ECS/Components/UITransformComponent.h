#pragma once
// ===========================================================================
// 概要: UI 要素の矩形をアンカー方式で表すコンポーネント UITransformComponent。
//
// 責務:
//   - 親（画面・Canvas・親 UI）の中での相対的な配置を保持する
//   - UILayoutSystem が計算した画面上の矩形（computed*）を保持する
//
// 備考:
//   - anchor: 親の矩形内の基準点 [0,1]。{0,0}=左上、{0.5,0.5}=中央、{1,1}=右下。
//   - pivot : 自身の基準点 [0,1]。回転と配置の中心。
//   - offset: anchor からのオフセット（px）。解像度が変わっても端からの距離を保てる。
//   - 点アンカー + サイズのみ対応。min/max を別に指定するストレッチは未対応。
// ===========================================================================
#include <DirectXMath.h>

struct UITransformComponent
{
    // 配置（保存対象）
    DirectX::XMFLOAT2 anchor   = {0.5f, 0.5f};    // 親の矩形内の基準点 [0,1]
    DirectX::XMFLOAT2 pivot    = {0.5f, 0.5f};    // 自身の基準点 [0,1]
    DirectX::XMFLOAT2 offset   = {0.0f, 0.0f};    // anchor からのオフセット（px）
    DirectX::XMFLOAT2 size     = {200.0f, 60.0f}; // 要素のサイズ（px）
    float             rotation = 0.0f;            // 回転（度）。pivot を中心に回す
    float             scale    = 1.0f;            // 拡大率（子へも掛かる）

    // 不透明度 [0,1]（保存しない。出現アニメ等が動かす）。収集時に画像・文字の色の α に掛ける。
    //   色の α とは別の値なので、ボタン色が毎フレーム上書きされてもフェードできる
    float alpha = 1.0f;

    // 以下は UILayoutSystem が書くレイアウト結果（保存しない）
    DirectX::XMFLOAT2 computedPos  = {0.0f, 0.0f}; // 画面上の左上位置（px）
    DirectX::XMFLOAT2 computedSize = {0.0f, 0.0f}; // 画面上のサイズ（px）
};
