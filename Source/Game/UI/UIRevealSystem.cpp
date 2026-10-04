#include "UIRevealSystem.h"

#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

#include "Engine/UI/ECS/Components/UITextComponent.h"

#include <algorithm>

using namespace CCL::ECS;

void UIRevealSystem::Update(float dt)
{
    ForEachWithID([&](EntityID id, UIRevealComponent &rv, UITransformComponent &t) {
        rv.elapsed += dt;
        if (!rv.captured)
        {
            rv.baseX    = t.offset.x;
            rv.captured = true;
        }

        const float local = rv.elapsed - rv.delay;                                     // 出始めてからの秒数
        const float a     = std::clamp(local / std::max(rv.duration, 0.0001f), 0.0f, 1.0f); // SlideIn / Grow の進み具合
        const float eased = 1.0f - (1.0f - a) * (1.0f - a);                            // 減速しながら止まる

        switch (rv.mode)
        {
        case UIRevealMode::Type:
        {
            auto *tx = m_world->GetComponent<UITextComponent>(id);
            if (!tx) break;
            const float n     = std::clamp(local * rv.speed, 0.0f, static_cast<float>(rv.text.size()));
            const auto  count = static_cast<size_t>(n);
            if (tx->text.size() != count) tx->text = rv.text.substr(0, count);
            break;
        }
        case UIRevealMode::SlideIn:
            t.offset.x = rv.baseX - (1.0f - eased) * rv.distance;
            t.alpha    = a;
            break;

        case UIRevealMode::Grow:
        {
            const float w = rv.length * eased;
            t.size.x      = w;
            t.offset.x    = rv.baseX + w * 0.5f; // 中心で置くので、左端を baseX に止める
            if (auto *tipT = m_world->GetComponent<UITransformComponent>(rv.tip))
            {
                tipT->offset.x = rv.baseX + w;
                tipT->alpha    = (local > 0.0f) ? 1.0f : 0.0f;
            }
            break;
        }
        }
    });
}

REGISTER_RENDER_SYSTEM(UIRevealSystem, Priority::RenderStage::R10_UI);
