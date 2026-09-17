// 役割・設計方針は UILayoutSystem.h を参照。
#include "UILayoutSystem.h"

#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

#include "Engine/Graphics/RHI/Graphics.h"
#include "Engine/UI/ECS/UIDrawList.h"
#include "Engine/UI/ECS/UILayoutUtil.h"

using namespace CCL::ECS;

void UILayoutSystem::Initialize()
{
    // 描画リストのリソースをここで用意（未登録時のみ）。収集Systemが積み、UIRenderer が2Dパスで読む。
    if (!m_world->HasResource<UIDrawList>())
        m_world->AddResource<UIDrawList>();
}

void UILayoutSystem::Update(float /*dt*/)
{
    auto       &gfx     = Graphics::Instance();
    const float screenW = gfx.GetScreenWidth();
    const float screenH = gfx.GetScreenHeight();

    // 全UI要素の矩形を確定させる。以降のSystem（収集・入力判定）は computed* を読むだけ。
    ForEachWithID([&](CCL::ECS::EntityID id, UITransformComponent &t) {
        CCL::UI::ComputeUIRect(*m_world, id, t, screenW, screenH);
    });
}

// R10_UI（UpdateRender中）で矩形確定のみ。描画リストへ積むのは次の帯(R10b_UICollect)。
REGISTER_RENDER_SYSTEM(UILayoutSystem, Priority::RenderStage::R10_UI);
