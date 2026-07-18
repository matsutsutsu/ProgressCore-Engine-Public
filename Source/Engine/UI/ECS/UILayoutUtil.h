#pragma once
/// @file UILayoutUtil.h
/// @brief アンカー方式のUI矩形→画面ピクセルの計算（画像/テキスト収集で共用）。
#include "Engine/UI/ECS/Components/UITransformComponent.h"

namespace CCL::UI
{
/// @brief UITransform の anchor/pivot/offset/size から画面px矩形を計算し computed* へ書く。
///        Slice 1/2 は「点アンカー＋サイズ」方式（ストレッチは後続）。
inline void ComputeUIRect(UITransformComponent &t, float screenW, float screenH)
{
    const float w = t.size.x * t.scale;
    const float h = t.size.y * t.scale;

    const float anchorPxX = t.anchor.x * screenW;
    const float anchorPxY = t.anchor.y * screenH;

    // pivot 点が (anchor + offset) に来るよう左上を逆算
    const float pivotPxX = anchorPxX + t.offset.x;
    const float pivotPxY = anchorPxY + t.offset.y;

    t.computedPos  = {pivotPxX - t.pivot.x * w, pivotPxY - t.pivot.y * h};
    t.computedSize = {w, h};
}
} // namespace CCL::UI
