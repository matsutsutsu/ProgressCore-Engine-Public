/// @file UIAnimationSystem.cpp
/// 役割・設計方針は UIAnimationSystem.h を参照。
#include "UIAnimationSystem.h"

#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

#include <cmath>

using namespace CCL::ECS;

void UIAnimationSystem::Update(float dt)
{
    if (dt <= 0.0f) return;

    ForEach([&](UITransformComponent &t, const UIAnimationComponent &a) {
        // 指数減衰でフレームレート非依存に目標へ近づける（k = 1 - e^(-speed*dt)）。
        const float k = 1.0f - std::exp(-a.smoothSpeed * dt);
        t.scale += (a.targetScale - t.scale) * k;
    });
}

REGISTER_RENDER_SYSTEM(UIAnimationSystem, Priority::RenderStage::R10_UI);
