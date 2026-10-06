#include "Game/Logics/Cinematic/CinematicFrameSystem.h"

#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include "Engine/Core/App/GameFlowState.h"
#include "Engine/Core/Math/Interpolation.h" // SmoothStep
#include "Engine/Graphics/RHI/Graphics.h"
#include "Engine/Time/TimeService.h" // Time::PresentationDt（ポーズ中は止める）
#include "Engine/UI/ECS/UIDrawList.h"

#include <algorithm>

using namespace CCL::ECS;

void CinematicFrameSystem::Update(float dt)
{
    if (!m_world->HasResource<UIDrawList>()) return;
    UIDrawList &list = m_world->GetResource<UIDrawList>();

    // --- 出具合を進める（ムービー中は出す、終わったら引っ込める） ---
    const bool cinematic = m_world->HasResource<GameFlowState>() && m_world->GetResource<GameFlowState>().cinematic;
    dt                   = Time::PresentationDt(*m_world, dt);
    m_amount             = cinematic ? std::min(m_amount + dt / kInTime, 1.0f) : std::max(m_amount - dt / kOutTime, 0.0f);

    const float k = CCL::Math::SmoothStep(m_amount); // 動き始めと止まり際をゆるやかに

    // HUD は黒帯と同じ進み具合で薄くする（ムービーの間は画面に出さない）
    list.hudAlpha = 1.0f - k;
    if (k <= 0.0f) return;

    // --- 上下の黒帯 ---
    auto       &gfx = Graphics::Instance();
    const float sw  = gfx.GetScreenWidth();
    const float sh  = gfx.GetScreenHeight();
    const float h   = sh * kBarRatio * k;

    UIDrawItem bar;
    bar.kind      = UIDrawKind::Image;
    bar.color     = {0.0f, 0.0f, 0.0f, 1.0f};
    bar.sortOrder = kSortOrder;
    bar.size      = {sw, h};

    bar.pos = {0.0f, 0.0f}; // 上
    list.items.push_back(bar);
    bar.pos = {0.0f, sh - h}; // 下
    list.items.push_back(bar);
}

REGISTER_RENDER_SYSTEM(CinematicFrameSystem, Priority::RenderStage::R10b_UICollect);
