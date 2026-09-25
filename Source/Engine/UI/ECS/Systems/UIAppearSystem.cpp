#include "UIAppearSystem.h"

#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

#include <algorithm>

using namespace CCL::ECS;

void UIAppearSystem::Update(float dt)
{
    if (dt <= 0.0f) return;

    ForEach([&](UITransformComponent &t, UIAppearComponent &a) {
        if (!a.play || a.done) return;

        // 初回は基準の offset.y を控え、開始状態（透明・少し下）に置く
        if (!a.captured)
        {
            a.captured    = true;
            a.baseOffsetY = t.offset.y;
            t.alpha       = 0.0f;
            t.offset.y    = a.baseOffsetY + a.slideY;
        }

        a.elapsed += dt;
        const float local = a.elapsed - a.delay; // delay 経過前は負
        if (local < 0.0f) return;                // 開始前は透明のまま待つ

        const float p = (a.duration > 0.0f) ? (std::min)(local / a.duration, 1.0f) : 1.0f;
        // easeOutCubic: 最初が速く、最後に減速して止まる
        const float ease = 1.0f - (1.0f - p) * (1.0f - p) * (1.0f - p);

        t.alpha    = ease;                                     // 色の α に掛ける値なので、元の透明度は保たれる
        t.offset.y = a.baseOffsetY + a.slideY * (1.0f - ease); // 下から定位置へ上がる

        if (p >= 1.0f)
        {
            t.alpha    = 1.0f;
            t.offset.y = a.baseOffsetY;
            a.done     = true; // 1 回きり。以降は触らない
        }
    });
}

REGISTER_RENDER_SYSTEM(UIAppearSystem, Priority::RenderStage::R10_UI);
