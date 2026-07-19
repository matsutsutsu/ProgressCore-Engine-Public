#pragma once
/// @file UIProgressBarComponent.h
/// @brief 汎用ゲージ（背景＋横方向フィル）。HUDのHP/ゲージ等に使う再利用ウィジェット。
///
/// 設計方針:
///   - value(0..1) と2色だけを持つデータ。UIProgressBarCollectSystem が UITransform の矩形から
///     背景矩形＋幅×value のフィル矩形の2枚を UIDrawList へ積む（描画は既存の単色Image経路）。
///   - 値の供給（データ連動）は別System（例: Game側 OverdriveHudSystem）が担う（表現と供給の分離）。
///   - showTrail=true で「遅延ダメージ（チップ）バー」を1枚追加できる。背景とフィルの間に
///     幅×trailValue の帯を描き、被弾で value が即減った後 trailValue を遅れて追従させると、
///     失った分（value..trailValue）が一瞬色付きで残る。trailValue の駆動は供給側Systemが行う。
#include <DirectXMath.h>

struct UIProgressBarComponent
{
    float             value     = 1.0f;                     ///< 充填率 0..1
    DirectX::XMFLOAT4 bgColor   = {0.0f, 0.0f, 0.0f, 0.63f}; ///< 背景色
    DirectX::XMFLOAT4 fillColor = {0.27f, 0.67f, 1.0f, 1.0f}; ///< フィル色
    bool              visible   = true;
    int               sortOrder = 0; ///< 背景=これ / フィル=これ(+1 or +2)（大きいほど手前）

    // --- 遅延ダメージ（チップ）バー（既定OFF。HPゲージ等で使う） ---
    bool              showTrail  = false;                     ///< 遅延バーを描くか
    float             trailValue = 1.0f;                      ///< 遅延バーの充填率 0..1（供給側が遅れて追従）
    DirectX::XMFLOAT4 trailColor = {1.0f, 0.85f, 0.85f, 0.95f}; ///< 遅延バーの色（失った分＝淡い白赤）
};
