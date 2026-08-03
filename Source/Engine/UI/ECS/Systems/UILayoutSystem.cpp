/// @file UILayoutSystem.cpp
/// 役割・設計方針は UILayoutSystem.h を参照。
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
    // 描画リストのリソースをここで用意（未登録時のみ）。UIRenderer が2Dパスで読む。
    if (!m_world->HasResource<UIDrawList>())
        m_world->AddResource<UIDrawList>();
}

void UILayoutSystem::Update(float /*dt*/)
{
    if (!m_world->HasResource<UIDrawList>())
        return;

    auto       &gfx     = Graphics::Instance();
    const float screenW = gfx.GetScreenWidth();
    const float screenH = gfx.GetScreenHeight();

    auto &drawList = m_world->GetResource<UIDrawList>();
    // ※ クリアは消費側(UIRenderer::Flush 末尾)が行う。ここでは積むだけ。

    ForEachWithID([&](CCL::ECS::EntityID id, UITransformComponent &t, const UIImageComponent &img) {
        CCL::UI::ComputeUIRect(*m_world, id, t, screenW, screenH);

        if (!img.visible) return;

        UIDrawItem item;
        item.kind        = UIDrawKind::Image;
        item.pos         = t.computedPos;
        item.size        = t.computedSize;
        item.color       = img.color;
        item.color.w    *= t.alpha; // 演出用の不透明度を反映（フェードイン等）
        item.rotation    = t.rotation;
        item.sortOrder   = img.sortOrder;
        item.texturePath = img.texturePath;
        drawList.items.push_back(std::move(item));
    });
}

// R10_UI（UpdateRender中）で矩形確定＋描画リスト構築。直接描画はしない。
REGISTER_RENDER_SYSTEM(UILayoutSystem, Priority::RenderStage::R10_UI);
