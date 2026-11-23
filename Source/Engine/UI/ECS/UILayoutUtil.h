#pragma once
// アンカー方式の UI 矩形を、画面上のピクセル矩形へ変換する関数。呼ぶのは UILayoutSystem だけ
// Canvas は画面全体を基準に offset で動き、scale は子へ継承する（子の実効 scale = 親 × 自身）
#include "Engine/UI/ECS/Components/UITransformComponent.h"
#include "Engine/UI/ECS/Components/UICanvasComponent.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/ECS/Core/CCL_World.h"

namespace CCL::UI
{
    // UI の矩形（画面上の px。左上原点）
    struct UIRect
    {
        float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
    };

    // 矩形の計算結果。rect は画面上の px 矩形、scale は実効スケール（親から継承した倍率 × 自身の scale）
    struct UIResolved
    {
        UIRect rect;
        float  scale = 1.0f;
    };

    // 親をたどる深さの上限。UI の入れ子は Canvas → パネル → 部品の数段しか要らないので、
    // これを超えるのは親子リンクが循環している（A の親が B、B の親が A）とき
    // ヒエラルキーのドラッグ&ドロップで作れてしまい、そのまま再帰するとスタックオーバーフローで
    // クラッシュするため、ここで打ち切って画面基準にする
    inline constexpr int kMaxUIDepth = 16;

    // Entity の UI の矩形と実効のスケールを、親をたどって求める。基準にする矩形は、
    //   自分が UICanvasComponent を持つ   : 画面全体を offset で動かし、scale で拡縮したもの
    //   親が UITransformComponent を持つ : 親の矩形と実効のスケール
    //   それ以外                         : 画面全体で、scale は 1
    inline UIResolved ResolveUINode(CCL::ECS::World &world, CCL::ECS::EntityID id,
                                    const UITransformComponent &t, float screenW, float screenH,
                                    int depth = 0)
    {
        // Canvas は画面全体に自身の offset / scale を掛けたものが矩形になる
        if (world.HasComponent<UICanvasComponent>(id))
        {
            return {{t.offset.x, t.offset.y, screenW * t.scale, screenH * t.scale}, t.scale};
        }

        // 親の矩形とスケールを求める（親が UI でなければ画面全体・等倍）
        UIRect parentRect{0.0f, 0.0f, screenW, screenH};
        float  parentScale = 1.0f;

        // depth が上限に達したら親を見ない（親子の循環対策）
        if (depth < kMaxUIDepth)
        {
            if (auto *tr = world.GetComponent<TransformComponent>(id))
            {
                const CCL::ECS::EntityID pid = tr->parent;
                if (pid != 0 && pid != CCL::ECS::InvalidEntityID && pid != id)
                {
                    if (auto *pt = world.GetComponent<UITransformComponent>(pid))
                    {
                        const UIResolved p = ResolveUINode(world, pid, *pt, screenW, screenH, depth + 1);
                        parentRect         = p.rect;
                        parentScale        = p.scale;
                    }
                }
            }
        }

        // 自身の矩形を親の矩形の中で決める
        const float eff = parentScale * t.scale;
        const float w   = t.size.x * eff;
        const float h   = t.size.y * eff;

        // anchor は親の矩形に対する比率なので、親の移動・拡縮に追従する。offset は親のスケールで拡縮する
        const float anchorPxX = parentRect.x + t.anchor.x * parentRect.w;
        const float anchorPxY = parentRect.y + t.anchor.y * parentRect.h;
        const float pivotPxX  = anchorPxX + t.offset.x * parentScale;
        const float pivotPxY  = anchorPxY + t.offset.y * parentScale;

        return {{pivotPxX - t.pivot.x * w, pivotPxY - t.pivot.y * h, w, h}, eff};
    }

    // 親子をたどって求めた矩形で UITransform の computed* を埋める
    inline void ComputeUIRect(CCL::ECS::World &world, CCL::ECS::EntityID id,
                              UITransformComponent &t, float screenW, float screenH)
    {
        const UIRect r = ResolveUINode(world, id, t, screenW, screenH).rect;
        t.computedPos  = {r.x, r.y};
        t.computedSize = {r.w, r.h};
    }
}
