#pragma once
/// @file UITransformComponent.h
/// @brief ECSネイティブUIの矩形トランスフォーム（解像度非依存のアンカー方式）。
///
/// 設計方針:
///   - UIは「画面（またはCanvas）内での相対位置」で持つ。実ピクセルは UILayoutSystem が
///     画面サイズから毎フレーム計算し computed* に書き込む（データと計算の分離）。
///   - anchor: 画面内の基準点 (0..1)。例 {0,0}=左上, {0.5,0.5}=中央, {1,1}=右下。
///   - pivot : 自身の基準点 (0..1)。回転・配置の中心。
///   - offset: アンカーからのピクセルオフセット。解像度が変わっても端からの距離を保てる。
///   ※ Slice 1 では「点アンカー＋サイズ」のみ対応。ストレッチ（min/max別指定）は後続で追加する。
#include <DirectXMath.h>

struct UITransformComponent
{
    // --- レイアウト（シリアライズ対象） ---
    DirectX::XMFLOAT2 anchor = {0.5f, 0.5f}; ///< 画面内の基準点 (0..1)
    DirectX::XMFLOAT2 pivot  = {0.5f, 0.5f}; ///< 自身の基準点 (0..1)
    DirectX::XMFLOAT2 offset = {0.0f, 0.0f}; ///< アンカーからのオフセット(px)
    DirectX::XMFLOAT2 size   = {200.0f, 60.0f}; ///< 要素サイズ(px)
    float             rotation = 0.0f;       ///< 回転(度)。pivot 基準で回す。
    float             scale    = 1.0f;       ///< 拡大率

    // --- 演出用（非シリアライズ：出現アニメ等が動かす） ---
    ///< 不透明度(0..1)。収集時に画像/文字の色αへ乗算される。フェードイン等の演出チャンネル。
    ///  色そのものの α とは独立なので、ボタン色の毎フレーム上書きに影響されずフェードできる。
    float             alpha    = 1.0f;

    // --- レイアウト結果（非シリアライズ：UILayoutSystem が書き込む） ---
    DirectX::XMFLOAT2 computedPos  = {0.0f, 0.0f}; ///< 画面上の左上位置(px)
    DirectX::XMFLOAT2 computedSize = {0.0f, 0.0f}; ///< 画面上のサイズ(px)
};
