#include "UITextCollectSystem.h"

#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

#include "Engine/UI/ECS/UIDrawList.h"

#include <algorithm>

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

        // 影は本体と同じ sortOrder で先に積む。並べ替えは安定ソートなので、影が本体のすぐ後ろに来る。
        //   ずらす量を文字の高さに比例させるのは、固定 px だと小さい文字では影が離れすぎて二重に見えるため
        if (txt.shadow)
        {
            const float off    = std::max(1.0f, txt.pixelSize * 0.08f);
            UIDrawItem  shadow = item;
            shadow.pos         = {item.pos.x + off, item.pos.y + off};
            shadow.color       = {0.0f, 0.0f, 0.0f, item.color.w * 0.75f};
            drawList.items.push_back(std::move(shadow));
        }
        drawList.items.push_back(std::move(item));
    });
}

REGISTER_RENDER_SYSTEM(UITextCollectSystem, Priority::RenderStage::R10b_UICollect);
