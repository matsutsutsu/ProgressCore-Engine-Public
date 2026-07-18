#pragma once
/// @file UIProgressBarComponent.h
/// @brief 汎用ゲージ（背景＋横方向フィル）。HUDのHP/ゲージ等に使う再利用ウィジェット。
///
/// 設計方針:
///   - value(0..1) と2色だけを持つデータ。UIProgressBarCollectSystem が UITransform の矩形から
///     背景矩形＋幅×value のフィル矩形の2枚を UIDrawList へ積む（描画は既存の単色Image経路）。
///   - 値の供給（データ連動）は別System（例: Game側 OverdriveHudSystem）が担う（表現と供給の分離）。
#include <DirectXMath.h>

struct UIProgressBarComponent
{
    float             value     = 1.0f;                     ///< 充填率 0..1
    DirectX::XMFLOAT4 bgColor   = {0.0f, 0.0f, 0.0f, 0.63f}; ///< 背景色
    DirectX::XMFLOAT4 fillColor = {0.27f, 0.67f, 1.0f, 1.0f}; ///< フィル色
    bool              visible   = true;
    int               sortOrder = 0; ///< 背景=これ / フィル=これ+1（大きいほど手前）
};
