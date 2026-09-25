#include "UIRenderer.h"

#include "Engine/Graphics/RHI/RenderState.h"
#include "Engine/Graphics/Renderer/Sprite.h"
#include "Engine/Platform/Core/Logger.h"
#include "Engine/UI/ECS/UIDrawList.h"
#include "Engine/UI/Text/TextRenderer.h"

#include <algorithm>
#include <filesystem>

// 既定のフォント。日本語も含むフォントだが、TextRenderer が焼くのは ASCII だけ
static const char *kDefaultFontPath = "Assets/Fonts/RictyDiminished-Regular.ttf";

UIRenderer::UIRenderer()  = default;
UIRenderer::~UIRenderer() = default;

Sprite *UIRenderer::GetWhite(ID3D11Device *device)
{
    if (!m_white)
        m_white = std::make_unique<Sprite>(device);
    return m_white.get();
}

Sprite *UIRenderer::GetSprite(ID3D11Device *device, const std::string &path)
{
    if (path.empty())
        return GetWhite(device); // 単色塗り

    auto it = m_cache.find(path);
    if (it != m_cache.end())
        return it->second ? it->second.get() : GetWhite(device); // null は読めなかったパス

    // 読み込む前にファイルの有無を確かめる。
    //   texturePath はインスペクタから手入力できるので、打ち間違いが起きる。
    //   Sprite のコンストラクタは失敗を返さず、Debug では _ASSERT_EXPR で止まり、
    //   Release では未初期化の D3D11_TEXTURE2D_DESC から幅・高さを読んでしまう
    //   （その壊れた Sprite がキャッシュに残り続ける）。
    //   ここで弾いて白テクスチャで代用し、UI が消えるのではなく白い矩形が出ることで気付けるようにする
    std::error_code ec;
    if (!std::filesystem::exists(path, ec))
    {
        CCL_LOG_WARN(LogCategory::Render,
                     "[UIRenderer] テクスチャが見つかりません: '%s'（白ダミーで代替）", path.c_str());
        m_cache.emplace(path, nullptr); // 失敗も記録し、毎フレーム同じ警告を出さない
        return GetWhite(device);
    }

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
        if (pass == BlendPass::Normal && item->additive) continue;
        if (pass == BlendPass::Additive && !item->additive) continue;

        if (item->kind == UIDrawKind::Image)
        {
            Sprite *sprite = GetSprite(device, item->texturePath);
            if (!sprite) continue;

            // uvRect は正規化 [0,1]。Sprite の切り抜きはピクセル指定なので、ここでテクスチャの実サイズを掛ける
            const float texW = sprite->GetWidth();
            const float texH = sprite->GetHeight();

            // 上端は color、下端は colorBottom。グラデーションなしなら 4 隅とも同じ色
            const DirectX::XMFLOAT4 top        = item->color;
            const DirectX::XMFLOAT4 bot        = item->useGradient ? item->colorBottom : item->color;
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

    // sortOrder 昇順に並べる。同じ sortOrder は積まれた順を保つ（安定ソート）
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
        // 加算が無い（または合成を切り替える手段が無い）なら 1 回で全て描く。
        // renderState が無いときに additive のアイテムも描くのは、発光しないより消える方が問題だから
        DrawItems(dc, device, ordered, BlendPass::All);
    }
    else
    {
        // 通常合成 → 加算合成の順に描く。加算は下にある絵に足して初めて光って見えるため、
        // sortOrder に関係なく最後にまとめる
        DrawItems(dc, device, ordered, BlendPass::Normal);

        dc->OMSetBlendState(renderState->GetBlendState(BlendState::Additive), nullptr, 0xffffffff);
        DrawItems(dc, device, ordered, BlendPass::Additive);

        // 呼び出し側（BaseScene の 2D パス）が設定していた合成へ戻す。
        // 戻さないと、この後の OnRenderUI が加算のまま描かれる
        dc->OMSetBlendState(renderState->GetBlendState(BlendState::UITransparency), nullptr, 0xffffffff);
    }

    // 次のフレームは収集 System が空のリストから積み直す
    list.Clear();
}
