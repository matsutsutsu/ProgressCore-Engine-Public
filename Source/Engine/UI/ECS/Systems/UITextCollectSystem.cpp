#include "UITextCollectSystem.h"

#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

#include "Engine/UI/ECS/UIDrawList.h"

using namespace CCL::ECS;

void UITextCollectSystem::Update(float /*dt*/)
{
    if (!m_world->HasResource<UIDrawList>())
        return;

    // クリアは UIRenderer::Flush の末尾で行うので、ここでは積むだけ
    auto &drawList = m_world->GetResource<UIDrawList>();

    ForEach([&](const UITransformComponent &t, const UITextComponent &txt) {
        if (!txt.visible || txt.text.empty()) return;

        UIDrawItem item;
        item.kind  = UIDrawKind::Text;
        item.pos   = t.computedPos; // 左上から描く
        item.color = txt.color;
        item.color.w *= t.alpha; // 出現アニメ等の不透明度を掛ける
        item.sortOrder = txt.sortOrder;
        item.text      = txt.text;
        item.pixelSize = txt.pixelSize;
        drawList.items.push_back(std::move(item));
    });
}

REGISTER_RENDER_SYSTEM(UITextCollectSystem, Priority::RenderStage::R10b_UICollect);
