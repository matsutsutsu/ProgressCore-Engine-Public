/// @file UITextCollectSystem.cpp
/// 役割・設計方針は UITextCollectSystem.h を参照。
#include "UITextCollectSystem.h"

#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

#include "Engine/Graphics/RHI/Graphics.h"
#include "Engine/UI/ECS/UIDrawList.h"
#include "Engine/UI/ECS/UILayoutUtil.h"

using namespace CCL::ECS;

void UITextCollectSystem::Update(float /*dt*/)
{
    if (!_world->HasResource<UIDrawList>())
        return;

    auto       &gfx     = Graphics::Instance();
    const float screenW = gfx.GetScreenWidth();
    const float screenH = gfx.GetScreenHeight();

    auto &drawList = _world->GetResource<UIDrawList>();
    // ※ クリアは消費側(UIRenderer::Flush 末尾)が行う。

    ForEachWithID([&](CCL::ECS::EntityID id, UITransformComponent &t, const UITextComponent &txt) {
        CCL::UI::ComputeUIRect(*_world, id, t, screenW, screenH);

        if (!txt.visible || txt.text.empty()) return;

        UIDrawItem item;
        item.kind      = UIDrawKind::Text;
        item.pos       = t.computedPos; // 左上基準
        item.color     = txt.color;
        item.color.w  *= t.alpha; // 演出用の不透明度を反映（フェードイン等）
        item.sortOrder = txt.sortOrder;
        item.text      = txt.text;
        item.pixelSize = txt.pixelSize;
        drawList.items.push_back(std::move(item));
    });
}

REGISTER_RENDER_SYSTEM(UITextCollectSystem, Priority::RenderStage::R10_UI);
