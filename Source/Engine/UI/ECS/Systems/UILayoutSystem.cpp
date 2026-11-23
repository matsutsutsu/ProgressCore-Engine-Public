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
    // 収集 System が積み、UIRenderer が 2D パスで読む描画リストを用意する
    if (!m_world->HasResource<UIDrawList>())
        m_world->AddResource<UIDrawList>();
}



void UILayoutSystem::Update(float dt)
{
    auto       &gfx     = Graphics::Instance();
    const float screenW = gfx.GetScreenWidth();
    const float screenH = gfx.GetScreenHeight();

    // 全 UI 要素の矩形を確定させる。以降の System（収集・次フレームの入力判定）は computed* を読むだけ
    ForEachWithID([&](CCL::ECS::EntityID id, UITransformComponent &t) {
        CCL::UI::ComputeUIRect(*m_world, id, t, screenW, screenH);
    });
}



// 描画リストへ積むのは次の帯（R10b_UICollect）の収集 System
REGISTER_RENDER_SYSTEM(UILayoutSystem, Priority::RenderStage::R10_UI);
