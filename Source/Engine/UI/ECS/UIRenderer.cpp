/// @file UIRenderer.cpp
/// 役割・設計方針は UIRenderer.h を参照。
#include "UIRenderer.h"

#include "Engine/Graphics/Renderer/Sprite.h"
#include "Engine/UI/ECS/UIDrawList.h"
#include "Engine/UI/Text/TextRenderer.h"

#include <algorithm>

// Slice 2 の既定フォント（日本語対応の Ricty。現状は ASCII を焼いて使用）。
static const char *kDefaultFontPath = "Assets/Fonts/RictyDiminished-Regular.ttf";

UIRenderer::UIRenderer()  = default;
UIRenderer::~UIRenderer() = default;

Sprite *UIRenderer::GetSprite(ID3D11Device *device, const std::string &path)
{
    if (path.empty())
    {
        if (!m_white)
            m_white = std::make_unique<Sprite>(device);
        return m_white.get();
    }

    auto it = m_cache.find(path);
    if (it != m_cache.end())
        return it->second.get();

    auto    sprite = std::make_unique<Sprite>(device, path.c_str());
    Sprite *ptr    = sprite.get();
    m_cache.emplace(path, std::move(sprite));
    return ptr;
}

TextRenderer *UIRenderer::GetText(ID3D11Device *device)
{
    if (!m_textTried)
    {
        m_textTried = true; // 失敗しても毎フレーム再試行しない
        auto tr     = std::make_unique<TextRenderer>();
        if (tr->Initialize(device, kDefaultFontPath))
            m_text = std::move(tr);
    }
    return m_text.get();
}

void UIRenderer::Flush(ID3D11DeviceContext *dc, ID3D11Device *device, UIDrawList &list)
{
    if (list.items.empty()) return;

    // sortOrder 昇順（安定ソートで登録順を維持）。画像/テキストを跨いで層を統一。
    std::vector<const UIDrawItem *> ordered;
    ordered.reserve(list.items.size());
    for (const auto &it : list.items)
        ordered.push_back(&it);
    std::stable_sort(ordered.begin(), ordered.end(),
        [](const UIDrawItem *a, const UIDrawItem *b) { return a->sortOrder < b->sortOrder; });

    for (const UIDrawItem *item : ordered)
    {
        if (item->kind == UIDrawKind::Image)
        {
            Sprite *sprite = GetSprite(device, item->texturePath);
            if (!sprite) continue;
            sprite->Render(dc,
                item->pos.x, item->pos.y, 0.0f,
                item->size.x, item->size.y,
                item->rotation,
                item->color.x, item->color.y, item->color.z, item->color.w);
        }
        else // Text
        {
            TextRenderer *tr = GetText(device);
            if (!tr || !tr->IsReady()) continue;
            tr->DrawText(dc, item->text, item->pos.x, item->pos.y, item->pixelSize, item->color);
        }
    }

    // 消費時クリア: 次フレームの収集Systemが空から積み直す（収集System間の順序に非依存）。
    list.Clear();
}
