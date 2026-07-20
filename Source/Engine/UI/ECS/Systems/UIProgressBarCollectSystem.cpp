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

using namespace CCL::ECS;

void UIProgressBarCollectSystem::Update(float /*dt*/)
{
    if (!_world->HasResource<UIDrawList>()) return;

    auto       &gfx     = Graphics::Instance();
    const float screenW = gfx.GetScreenWidth();
    const float screenH = gfx.GetScreenHeight();
    auto       &drawList = _world->GetResource<UIDrawList>();

    ForEachWithID([&](CCL::ECS::EntityID id, UITransformComponent &t, const UIProgressBarComponent &bar) {
        CCL::UI::ComputeUIRect(*_world, id, t, screenW, screenH);
        if (!bar.visible) return;

        const float v = std::clamp(bar.value, 0.0f, 1.0f);

        // 背景（矩形全体）
        UIDrawItem bg;
        bg.kind      = UIDrawKind::Image;
        bg.pos       = t.computedPos;
        bg.size      = t.computedSize;
        bg.color     = bar.bgColor;
        bg.sortOrder = bar.sortOrder;
        drawList.items.push_back(bg);

        // 遅延ダメージ（チップ）バー：背景とフィルの間に幅 × trailValue で描く。
        // フィルが value まで手前を覆うので、value..trailValue の差分だけが色付きで残る。
        if (bar.showTrail)
        {
            const float tv = std::clamp(bar.trailValue, 0.0f, 1.0f);
            UIDrawItem trail;
            trail.kind      = UIDrawKind::Image;
            trail.pos       = t.computedPos;
            trail.size      = {t.computedSize.x * tv, t.computedSize.y};
            trail.color     = bar.trailColor;
            trail.sortOrder = bar.sortOrder + 1; // 背景より手前・フィルより奥
            drawList.items.push_back(trail);
        }

        // フィル（幅 × value）。遅延バーがある時はその手前へ。
        UIDrawItem fill;
        fill.kind      = UIDrawKind::Image;
        fill.pos       = t.computedPos;
        fill.size      = {t.computedSize.x * v, t.computedSize.y};
        fill.color     = bar.fillColor;
        fill.sortOrder = bar.sortOrder + (bar.showTrail ? 2 : 1); // 遅延バー/背景より手前
        drawList.items.push_back(fill);
    });
}

REGISTER_RENDER_SYSTEM(UIProgressBarCollectSystem, Priority::RenderStage::R10_UI);
