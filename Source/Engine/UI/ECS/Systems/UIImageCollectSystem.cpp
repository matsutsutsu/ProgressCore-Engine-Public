// 役割・設計方針は UIImageCollectSystem.h を参照。
#include "UIImageCollectSystem.h"

#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

#include "Engine/UI/ECS/UIDrawList.h"

using namespace CCL::ECS;

void UIImageCollectSystem::Update(float /*dt*/)
{
    if (!m_world->HasResource<UIDrawList>()) return;

    auto &drawList = m_world->GetResource<UIDrawList>();
    // ※ クリアは消費側(UIRenderer::Flush 末尾)が行う。ここでは積むだけ。

    ForEach([&](const UITransformComponent &t, const UIImageComponent &img) {
        if (!img.visible) return;

        UIDrawItem item;
        item.kind  = UIDrawKind::Image;
        item.pos   = t.computedPos; // 矩形は UILayoutSystem が確定済み
        item.size  = t.computedSize;
        item.color = img.color;
        item.color.w *= t.alpha; // 演出用の不透明度を反映（フェードイン等）
        item.useGradient = img.useGradient;
        item.colorBottom = img.colorBottom;
        item.colorBottom.w *= t.alpha; // 下端にも同じフェードを掛ける（片側だけ残らないように）
        item.additive    = img.additive;
        item.rotation    = t.rotation;
        item.sortOrder   = img.sortOrder;
        item.texturePath = img.texturePath;
        drawList.items.push_back(std::move(item));
    });
}

REGISTER_RENDER_SYSTEM(UIImageCollectSystem, Priority::RenderStage::R10b_UICollect);
