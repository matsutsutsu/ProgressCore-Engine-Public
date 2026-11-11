#include "UIImageCollectSystem.h"

#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

#include "Engine/UI/ECS/UIDrawList.h"

using namespace CCL::ECS;

void UIImageCollectSystem::Update(float /*dt*/)
{
    if (!m_world->HasResource<UIDrawList>()) return;

    // クリアは UIRenderer::Flush の末尾で行うので、ここでは積むだけ
    auto &drawList = m_world->GetResource<UIDrawList>();

    ForEach([&](const UITransformComponent &t, const UIImageComponent &img) {
        if (!img.visible) return;

        UIDrawItem item;
        item.kind  = UIDrawKind::Image;
        item.pos   = t.computedPos;
        item.size  = t.computedSize;
        item.color = img.color;
        item.color.w *= t.alpha; // 出現アニメ等の不透明度を掛ける
        item.useGradient = img.useGradient;
        item.colorBottom = img.colorBottom;
        item.colorBottom.w *= t.alpha; // 下端にも掛ける（グラデーションの下側だけ残らないように）
        item.additive    = img.additive;
        item.rotation    = t.rotation;
        item.sortOrder   = img.sortOrder;
        item.texturePath = img.texturePath;
        item.uvRect      = img.uvRect;
        drawList.items.push_back(std::move(item));
    });
}



REGISTER_RENDER_SYSTEM(UIImageCollectSystem, Priority::RenderStage::R10b_UICollect);
