#pragma once
/// @file UILayoutUtil.h
/// @brief アンカー方式のUI矩形→画面ピクセルの計算（画像/テキスト/ゲージ収集で共用）。
///        親子(TransformComponent 階層)に対応し、子は親の computed 矩形と実効スケールを基準に配置する。
///        - Canvas(UICanvasComponent): 画面全体を基準にしつつ、自身の offset で平行移動・scale で拡縮する
///          「可動ルート」。Canvas を動かすと配下UIがまとめて動く。
///        - scale は階層継承する：子の実効scale = 親の実効scale × 自身のローカルscale。
#include "Engine/UI/ECS/Components/UITransformComponent.h"
#include "Engine/UI/ECS/Components/UICanvasComponent.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/ECS/Core/CCL_World.h"

namespace CCL::UI
{
/// @brief UI矩形（画面px。左上原点）。
struct UIRect
{
    float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
};

/// @brief 矩形解決の結果。rect は画面px矩形、scale はそのノードの「実効スケール」
///        （親から継承した倍率×自身のローカルscale）。子はこの scale をさらに継承する。
struct UIResolved
{
    UIRect rect;
    float  scale = 1.0f;
};

/// @brief エンティティのUI矩形と実効スケールを、親(TransformComponent.parent)をたどって解決する。
///        - UICanvasComponent → 画面全体を offset で平行移動・scale で拡縮した「可動ルート」。
///        - 親が UITransformComponent を持つ → その親矩形・親スケールを基準に入れ子配置。
///        - それ以外（親なし/UIでない）→ 画面全体を親矩形・scale=1 とする（後方互換の画面基準）。
inline UIResolved ResolveUINode(CCL::ECS::World &world, CCL::ECS::EntityID id,
    const UITransformComponent &t, float screenW, float screenH)
{
    // --- Canvas：画面全体を基準に、自身の offset/scale を「グループ変換」として適用 ---
    if (world.HasComponent<UICanvasComponent>(id))
    {
        return {{t.offset.x, t.offset.y, screenW * t.scale, screenH * t.scale}, t.scale};
    }

    // --- 親の矩形・スケールを求める（既定は画面全体・等倍＝従来挙動） ---
    UIRect parentRect{0.0f, 0.0f, screenW, screenH};
    float  parentScale = 1.0f;

    if (auto *tr = world.GetComponent<TransformComponent>(id))
    {
        const CCL::ECS::EntityID pid = tr->parent;
        if (pid != 0 && pid != CCL::ECS::InvalidEntityID)
        {
            if (auto *pt = world.GetComponent<UITransformComponent>(pid))
            {
                const UIResolved p = ResolveUINode(world, pid, *pt, screenW, screenH);
                parentRect  = p.rect;
                parentScale = p.scale;
            }
        }
    }

    // --- 自身の矩形を親矩形内で確定 ---
    // 実効スケール = 親の実効スケール × 自身のローカルスケール（scale の階層継承）。
    const float eff = parentScale * t.scale;
    const float w   = t.size.x * eff;
    const float h   = t.size.y * eff;

    // アンカー点は親矩形サイズに比例（親の移動/拡縮に自動追従）。offset は親スケールで拡縮する。
    const float anchorPxX = parentRect.x + t.anchor.x * parentRect.w;
    const float anchorPxY = parentRect.y + t.anchor.y * parentRect.h;
    const float pivotPxX  = anchorPxX + t.offset.x * parentScale;
    const float pivotPxY  = anchorPxY + t.offset.y * parentScale;

    return {{pivotPxX - t.pivot.x * w, pivotPxY - t.pivot.y * h, w, h}, eff};
}

/// @brief UITransform の computed* を階層解決した矩形で埋める（収集系が描画前に呼ぶ）。
inline void ComputeUIRect(CCL::ECS::World &world, CCL::ECS::EntityID id,
    UITransformComponent &t, float screenW, float screenH)
{
    const UIRect r = ResolveUINode(world, id, t, screenW, screenH).rect;
    t.computedPos  = {r.x, r.y};
    t.computedSize = {r.w, r.h};
}

/// @brief 【後方互換】親を考慮しない従来の画面基準計算（親なし相当・scale継承なし）。
inline void ComputeUIRect(UITransformComponent &t, float screenW, float screenH)
{
    const float w = t.size.x * t.scale;
    const float h = t.size.y * t.scale;
    const float anchorPxX = t.anchor.x * screenW;
    const float anchorPxY = t.anchor.y * screenH;
    const float pivotPxX  = anchorPxX + t.offset.x;
    const float pivotPxY  = anchorPxY + t.offset.y;
    t.computedPos  = {pivotPxX - t.pivot.x * w, pivotPxY - t.pivot.y * h};
    t.computedSize = {w, h};
}
} // namespace CCL::UI
