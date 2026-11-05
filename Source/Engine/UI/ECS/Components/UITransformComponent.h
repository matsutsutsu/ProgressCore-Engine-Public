#pragma once
#include <DirectXMath.h>

// UI 要素の矩形をアンカー方式で表す。computed* は UILayoutSystem が書く
// anchor は親の中の基準点（{0,0} が左上）、pivot は自身の基準点、offset は anchor からの距離（px）
// 点アンカーとサイズだけに対応し、min/max を別に指定するストレッチはない
struct UITransformComponent
{
    // 配置（保存対象）
    DirectX::XMFLOAT2 anchor   = {0.5f, 0.5f};    // 親の矩形内の基準点 [0,1]
    DirectX::XMFLOAT2 pivot    = {0.5f, 0.5f};    // 自身の基準点 [0,1]
    DirectX::XMFLOAT2 offset   = {0.0f, 0.0f};    // anchor からのオフセット（px）
    DirectX::XMFLOAT2 size     = {200.0f, 60.0f}; // 要素のサイズ（px）
    float             rotation = 0.0f;            // 回転（度）。pivot を中心に回す
    float             scale    = 1.0f;            // 拡大率（子へも掛かる）

    // 不透明度 [0,1]（保存しない。出現アニメ等が動かす）。収集時に画像・文字の色の α に掛ける
    //   色の α とは別の値なので、ボタン色が毎フレーム上書きされてもフェードできる
    float alpha = 1.0f;

    // 以下は UILayoutSystem が書くレイアウト結果（保存しない）
    DirectX::XMFLOAT2 computedPos  = {0.0f, 0.0f}; // 画面上の左上位置（px）
    DirectX::XMFLOAT2 computedSize = {0.0f, 0.0f}; // 画面上のサイズ（px）
};
