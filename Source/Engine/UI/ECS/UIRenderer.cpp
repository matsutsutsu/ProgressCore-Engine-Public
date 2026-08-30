/// @file UIRenderer.cpp
/// 役割・設計方針は UIRenderer.h を参照。
#include "UIRenderer.h"

#include "Engine/Graphics/RHI/RenderState.h"
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

void UIRenderer::DrawItems(ID3D11DeviceContext *dc, ID3D11Device *device,
                           const std::vector<const UIDrawItem *> &ordered, BlendPass pass)
{
    for (const UIDrawItem *item : ordered)
    {
        if (pass == BlendPass::Normal   &&  item->additive) continue;
        if (pass == BlendPass::Additive && !item->additive) continue;

        if (item->kind == UIDrawKind::Image)
        {
            Sprite *sprite = GetSprite(device, item->texturePath);
            if (!sprite) continue;

            // uvRect は正規化(0..1)。Sprite の切り抜きAPIはピクセル指定なので、
            // ここでテクスチャ実サイズを掛けて変換する（呼び出し側は解像度を知らなくて済む）。
            const float texW = sprite->GetWidth();
            const float texH = sprite->GetHeight();

            // 上端=color / 下端=colorBottom。単色なら4隅とも同じ色になる。
            const DirectX::XMFLOAT4 top = item->color;
            const DirectX::XMFLOAT4 bot = item->useGradient ? item->colorBottom : item->color;
            const DirectX::XMFLOAT4 corners[4] = {top, top, bot, bot}; // 左上/右上/左下/右下

            sprite->Render(dc,
                item->pos.x, item->pos.y, 0.0f,
                item->size.x, item->size.y,
                item->uvRect.x * texW, item->uvRect.y * texH,
                item->uvRect.z * texW, item->uvRect.w * texH,
                item->rotation,
                corners);
        }
        else // Text
        {
            TextRenderer *tr = GetText(device);
            if (!tr || !tr->IsReady()) continue;
            tr->DrawText(dc, item->text, item->pos.x, item->pos.y, item->pixelSize, item->color);
        }
    }
}

void UIRenderer::Flush(ID3D11DeviceContext *dc, ID3D11Device *device, UIDrawList &list,
                       RenderState *renderState)
{
    if (list.items.empty()) return;

    // sortOrder 昇順（安定ソートで登録順を維持）。画像/テキストを跨いで層を統一。
    std::vector<const UIDrawItem *> ordered;
    ordered.reserve(list.items.size());
    for (const auto &it : list.items)
        ordered.push_back(&it);
    std::stable_sort(ordered.begin(), ordered.end(),
        [](const UIDrawItem *a, const UIDrawItem *b) { return a->sortOrder < b->sortOrder; });

    const bool hasAdditive = renderState && std::any_of(ordered.begin(), ordered.end(),
        [](const UIDrawItem *i) { return i->additive; });

    if (!hasAdditive)
    {
        // 加算が無い（または合成を切り替える手段が無い）なら1巡で全部描く。
        // renderState が無い時に additive を弾かないのは、発光しないより消える方が事故だから。
        DrawItems(dc, device, ordered, BlendPass::All);
    }
    else
    {
        // 通常合成 → 加算合成の順。加算は「下に積まれた絵の上」で足されて初めて
        // 光って見えるため、sortOrder に関わらず必ず最後にまとめる。
        DrawItems(dc, device, ordered, BlendPass::Normal);

        dc->OMSetBlendState(renderState->GetBlendState(BlendState::Additive), nullptr, 0xffffffff);
        DrawItems(dc, device, ordered, BlendPass::Additive);

        // 呼び出し側（BaseScene の2Dパス）が設定していた合成へ戻す。
        // 戻さないと、この後の OnRenderUI が加算のまま描かれる。
        dc->OMSetBlendState(renderState->GetBlendState(BlendState::UITransparency), nullptr, 0xffffffff);
    }

    // 消費時クリア: 次フレームの収集Systemが空から積み直す（収集System間の順序に非依存）。
    list.Clear();
}
