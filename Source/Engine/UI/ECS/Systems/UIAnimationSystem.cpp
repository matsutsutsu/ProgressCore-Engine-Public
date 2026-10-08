#include "UIAnimationSystem.h"

#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include "Engine/Core/Math/Interpolation.h" // Damp(): フレームレート非依存の追従係数

#include <cmath>

using namespace CCL::ECS;

void UIAnimationSystem::Update(float dt)
{
    if (dt <= 0.0f) return;

    ForEach([&](UITransformComponent &t, const UIAnimationComponent &a) {
        const float k = CCL::Math::Damp(a.smoothSpeed, dt);
        t.scale += (a.targetScale - t.scale) * k;
    });
}

REGISTER_RENDER_SYSTEM(UIAnimationSystem, Priority::RenderStage::R10_UI);
