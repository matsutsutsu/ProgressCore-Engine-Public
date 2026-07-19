/// @file UIAppearSystem.cpp
/// 役割・設計方針は UIAppearSystem.h を参照。
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
        if (!a.play || a.done) return; // 無効 or 完了済みは何もしない

        // 初回：基準offsetを控え、開始状態（不可視・少し下）に置く。
        if (!a.captured)
        {
            a.captured    = true;
            a.baseOffsetY = t.offset.y;
            t.alpha       = 0.0f;
            t.offset.y    = a.baseOffsetY + a.slideY;
        }

        a.elapsed += dt;
        const float local = a.elapsed - a.delay; // delay 経過前は負
        if (local < 0.0f) return;                // 開始前は不可視のまま待機

        const float p = (a.duration > 0.0f) ? (std::min)(local / a.duration, 1.0f) : 1.0f;
        // easeOutCubic：最初速く、最後ふわっと減速して着地する。
        const float ease = 1.0f - (1.0f - p) * (1.0f - p) * (1.0f - p);

        t.alpha    = ease;                             // 0→1（色αへの乗算なので base 透明度を保つ）
        t.offset.y = a.baseOffsetY + a.slideY * (1.0f - ease); // 下→定位置へ上昇

        if (p >= 1.0f)
        {
            t.alpha    = 1.0f;
            t.offset.y = a.baseOffsetY;
            a.done     = true; // 1回きり。以降は触らない。
        }
    });
}

REGISTER_RENDER_SYSTEM(UIAppearSystem, Priority::RenderStage::R10_UI);
