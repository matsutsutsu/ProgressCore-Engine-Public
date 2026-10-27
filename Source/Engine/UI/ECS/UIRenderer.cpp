#include "UIRenderer.h"

#include "Engine/Graphics/RHI/RenderState.h"
#include "Engine/Graphics/Renderer/Sprite.h"
#include "Engine/Platform/Core/Logger.h"
#include "Engine/Platform/Core/FileSystem.h"
#include "Engine/UI/ECS/UIDrawList.h"
#include "Engine/UI/ECS/UIGaugeSprite.h"
#include "Engine/UI/Text/TextRenderer.h"

#include <algorithm>

static const char *kDefaultFontPath  = "Assets/Fonts/RictyDiminished-Regular.ttf"; // 半角英数字のフォント（等幅）
static const char *kFallbackFontPath = "Assets/Fonts/MPLUS1-Medium.ttf";           // 日本語など、それ以外の文字のフォント

UIRenderer::UIRenderer()  = default;
UIRenderer::~UIRenderer() = default;

// ---------------------------------------------------------------------------
// リソースの取得
// ---------------------------------------------------------------------------

Sprite *UIRenderer::GetWhite(ID3D11Device *device)
{
    if (!m_white)
    {
        m_white = std::make_unique<Sprite>(device);
    }
    return m_white.get();
}

Sprite *UIRenderer::GetSprite(ID3D11Device *device, const std::string &path)
{
    if (path.empty())
    {
        return GetWhite(device);
    }

    auto it = m_cache.find(path);
    if (it != m_cache.end())
    {
        return it->second ? it->second.get() : GetWhite(device);
    }

    // 読み込む前にファイルの有無を確かめる。
    // texturePath はインスペクタから手入力できるので、打ち間違いが起きる。

    // Sprite のコンストラクタは失敗を返さない。
    // Debug では _ASSERT_EXPR で止まり、Release では未初期化の D3D11_TEXTURE2D_DESC から幅・高さを読んでしまう。
    // その壊れた Sprite はキャッシュに残り続ける。

    // そこでここで弾き、白テクスチャで代用する。
    // UI が消えるのではなく白い四角形が出るので、打ち間違いに気付ける。
    if (!FileSystem::Exists(path))
    {
        CCL_LOG_WARN(LogCategory::Render,
                     "[UIRenderer] テクスチャが見つかりません: '%s'（白ダミーで代替）", path.c_str());

        // 読めなかったことも null で記録し、毎フレーム同じ警告を出さない。
        m_cache.emplace(path, nullptr);
        return GetWhite(device);
    }

    auto    sprite = std::make_unique<Sprite>(device, path.c_str());
    Sprite *ptr    = sprite.get();
    m_cache.emplace(path, std::move(sprite));
    return ptr;
}

UIGaugeSprite *UIRenderer::GetGauge(ID3D11Device *device)
{
    if (!m_gauge)
    {
        m_gauge = std::make_unique<UIGaugeSprite>(device);
    }
    return m_gauge.get();
}

TextRenderer *UIRenderer::GetText(ID3D11Device *device)
{
    // 初期化は1回だけ試す。フォントが無いときに、毎フレーム読み込みを試し直さない。
    if (!m_textTried)
    {
        m_textTried = true;
        auto tr     = std::make_unique<TextRenderer>();
        if (tr->Initialize(device, kDefaultFontPath, 48.0f, kFallbackFontPath))
        {
            m_text = std::move(tr);
        }
    }
    return m_text.get();
}

// ---------------------------------------------------------------------------
// 描画
// ---------------------------------------------------------------------------

void UIRenderer::DrawItems(ID3D11DeviceContext *dc, ID3D11Device *device,
                           const std::vector<const UIDrawItem *> &ordered, BlendPass pass)
{
    for (const UIDrawItem *item : ordered)
    {
        if (pass == BlendPass::Normal && item->additive)
        {
            continue;
        }
        if (pass == BlendPass::Additive && !item->additive)
        {
            continue;
        }

        if (item->kind == UIDrawKind::Image)
        {
            Sprite *sprite = GetSprite(device, item->texturePath);
            if (!sprite)
            {
                continue;
            }

            // uvRect は 0〜1 の割合で持つ。Sprite の切り抜きはピクセルで指定するので、テクスチャの実寸を掛ける。
            const float texW = sprite->GetWidth();
            const float texH = sprite->GetHeight();

            // 上端は color、下端は colorBottom の縦グラデーションにする。
            // 4隅を別々の色にはしない。UI の四角形は縦方向のグラデーションで足りる。
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
        else if (item->kind == UIDrawKind::Gauge)
        {
            GetGauge(device)->RenderGauge(dc, item->pos.x, item->pos.y, item->size.x, item->size.y,
                                          item->gauge, item->color.w);
        }
        else // Text
        {
            TextRenderer *tr = GetText(device);
            if (!tr || !tr->IsReady())
            {
                continue;
            }
            tr->DrawText(dc, item->text, item->pos.x, item->pos.y, item->pixelSize, item->color);
        }
    }
}

void UIRenderer::Flush(ID3D11DeviceContext *dc, ID3D11Device *device, UIDrawList &list,
                       RenderState *renderState)
{
    // 処理フロー:
    // 1. 画面全体の不透明度を掛け、描くアイテムを集める
    // 2. 描画順に並べる
    // 3. 通常合成・加算合成の順に描く
    // 4. 描画リストを空にする

    if (list.items.empty())
    {
        return;
    }

    // ---------------------------------------------------------
    // 1. 不透明度
    // ---------------------------------------------------------

    // 画面を重ねて入れ替える途中は、境目より奥と手前に、それぞれの不透明度を掛ける。
    // 設定画面が、下の画面の UI を薄くしながら自分を出すのに使う。

    // HUD（描画順が hudSortEnd より小さい要素）には、さらに HUD の不透明度を掛ける。
    // ムービーの間、HUD を薄くして隠すのに使う。

    // 部品ごとの visible や alpha では隠さない。
    // HUD などの System がそれらを毎フレーム書き戻すので、外から書いてもすぐ元に戻る。
    std::vector<const UIDrawItem *> ordered;
    ordered.reserve(list.items.size());
    for (auto &it : list.items)
    {
        float a = (it.sortOrder >= list.splitSortOrder) ? list.aboveAlpha : list.belowAlpha;
        if (it.sortOrder < list.hudSortEnd)
        {
            a *= list.hudAlpha;
        }
        if (a <= 0.0f)
        {
            continue;
        }
        it.color.w *= a;
        it.colorBottom.w *= a;
        ordered.push_back(&it);
    }

    // ---------------------------------------------------------
    // 2. 並べ替え
    // ---------------------------------------------------------

    // 同じ描画順のアイテムは、積まれた順を保つ（安定ソート）。
    std::stable_sort(ordered.begin(), ordered.end(),
                     [](const UIDrawItem *a, const UIDrawItem *b) { return a->sortOrder < b->sortOrder; });

    // ---------------------------------------------------------
    // 3. 描く
    // ---------------------------------------------------------
    const bool hasAdditive = renderState && std::any_of(ordered.begin(), ordered.end(),
                                                        [](const UIDrawItem *i) { return i->additive; });

    if (!hasAdditive)
    {
        // 加算が無い、または合成を切り替える手段が無いときは、1回で全部を描く。
        // renderState が無いときも additive のアイテムを描くのは、発光しないより消える方が困るため。
        DrawItems(dc, device, ordered, BlendPass::All);
    }
    else
    {
        // 加算は、下にある絵に足して初めて光って見える。
        // そこで通常合成のアイテムをすべて描いた後に、描画順に関係なくまとめて描く。
        DrawItems(dc, device, ordered, BlendPass::Normal);

        // 加算は、RT の α を書き換えない UI 用の加算（UIAdditive）にする。
        // Additive のままだと、透明な部分の多い画像（タイトルロゴを白く光らせる写しなど）で、
        // 画像の四角形の形に α の穴が開き、黒い四角に見える。
        dc->OMSetBlendState(renderState->GetBlendState(BlendState::UIAdditive), nullptr, 0xffffffff);
        DrawItems(dc, device, ordered, BlendPass::Additive);

        // 呼び出し側（BaseScene の 2D パス）が設定していた合成へ戻す。
        // 戻さないと、この後の OnRenderUI が加算のまま描かれる。
        dc->OMSetBlendState(renderState->GetBlendState(BlendState::UITransparency), nullptr, 0xffffffff);
    }

    // ---------------------------------------------------------
    // 4. 片付け
    // ---------------------------------------------------------

    // 次のフレームは、収集 System が空のリストから積み直す。
    list.Clear();
}
