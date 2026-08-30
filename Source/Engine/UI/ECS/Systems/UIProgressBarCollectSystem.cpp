/// @file UIProgressBarCollectSystem.cpp
/// 役割・設計方針は UIProgressBarCollectSystem.h を参照。
#include "UIProgressBarCollectSystem.h"

#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

#include "Engine/Graphics/RHI/Graphics.h"
#include "Engine/UI/ECS/UIDrawList.h"
#include "Engine/UI/ECS/UILayoutUtil.h"

#include <algorithm>
#include <cmath>

using namespace CCL::ECS;

void UIProgressBarCollectSystem::Update(float /*dt*/)
{
    if (!m_world->HasResource<UIDrawList>()) return;

    auto       &gfx     = Graphics::Instance();
    const float screenW = gfx.GetScreenWidth();
    const float screenH = gfx.GetScreenHeight();
    auto       &drawList = m_world->GetResource<UIDrawList>();

    ForEachWithID([&](CCL::ECS::EntityID id, UITransformComponent &t, const UIProgressBarComponent &bar) {
        CCL::UI::ComputeUIRect(*m_world, id, t, screenW, screenH);
        if (!bar.visible) return;

        const float v = std::clamp(bar.value, 0.0f, 1.0f);

        // 被弾の揺れ。ゲージを構成する矩形すべてに同じオフセットを足すので、
        // 部品がバラバラにずれず「ゲージが1つの物として殴られた」ように見える。
        //   XとYで振動数をずらしているのは、同じにすると斜め45度の直線往復になり、
        //   振動でなく「スライド」に見えてしまうため（軌跡をリサージュに崩す）。
        DirectX::XMFLOAT2 origin = t.computedPos;
        if (bar.shake > 0.0f)
        {
            const float amp   = bar.shakeAmplitude * bar.shake * bar.shake;
            const float phase = bar.shakeElapsed * bar.shakeFrequency * 6.2831853f;
            origin.x += std::sin(phase) * amp;
            origin.y += std::sin(phase * 1.7f) * amp * 0.5f; // 縦は控えめ＝横棒が跳ねて見えない
        }

        // 層の割り当ては固定にする（showTrail の有無で番号がズレると、
        // 同じ sortOrder を指定した外側の要素との前後関係が条件によって変わる）。
        //   +0 背景 / +1 遅延バー / +2 フィル
        const int kLayerBg    = bar.sortOrder;
        const int kLayerTrail = bar.sortOrder + 1;
        const int kLayerFill  = bar.sortOrder + 2;

        // 背景（矩形全体）
        UIDrawItem bg;
        bg.kind      = UIDrawKind::Image;
        bg.pos       = origin;
        bg.size      = t.computedSize;
        bg.color     = bar.bgColor;
        bg.sortOrder = kLayerBg;
        drawList.items.push_back(bg);

        // 遅延ダメージ（チップ）バー：背景とフィルの間に幅 × trailValue で描く。
        // フィルが value まで手前を覆うので、value..trailValue の差分だけが色付きで残る。
        if (bar.showTrail)
        {
            const float tv = std::clamp(bar.trailValue, 0.0f, 1.0f);
            UIDrawItem trail;
            trail.kind      = UIDrawKind::Image;
            trail.pos       = origin;
            trail.size      = {t.computedSize.x * tv, t.computedSize.y};
            trail.color     = bar.trailColor;
            trail.sortOrder = kLayerTrail;
            drawList.items.push_back(trail);
        }

        // フィル（幅 × value）。gradient>0 なら下端を暗くして立体感を出す。
        const float fillW = t.computedSize.x * v;
        UIDrawItem fill;
        fill.kind      = UIDrawKind::Image;
        fill.pos       = origin;
        fill.size      = {fillW, t.computedSize.y};
        fill.color     = bar.fillColor;
        fill.sortOrder = kLayerFill;
        if (bar.gradient > 0.0f)
        {
            const float k     = 1.0f - std::clamp(bar.gradient, 0.0f, 1.0f);
            fill.useGradient  = true;
            fill.colorBottom  = {bar.fillColor.x * k, bar.fillColor.y * k, bar.fillColor.z * k,
                                 bar.fillColor.w};
        }
        drawList.items.push_back(fill);
    });
}

REGISTER_RENDER_SYSTEM(UIProgressBarCollectSystem, Priority::RenderStage::R10_UI);
