// stb_truetype の実装はこの翻訳単位でだけ展開する。STBTT_STATIC で関数を static にし、
// ImGui が内部に持つ同名の stb 実装とリンク時に衝突しないようにする。
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "stb_truetype.h"

#include "TextRenderer.h"
#include "Engine/Graphics/Renderer/Sprite.h"

#include <wrl.h>
#include <fstream>
#include <vector>
#include <cstdint>

using Microsoft::WRL::ComPtr;

// stb に依存する状態はここに隠す
struct TextRenderer::Impl
{
    static constexpr int kFirst = 32; // ' '
    static constexpr int kCount = 96; // ' '..'~'

    bool  ready     = false;
    float bakePixel = 48.0f;
    int   atlasW    = 512;
    int   atlasH    = 512;

    stbtt_bakedchar         chars[kCount] = {};
    std::unique_ptr<Sprite> atlas;

    // 焼いたときの高さ（bakePixel）での縦方向のメトリクス（px）。descent は負
    float ascent  = 0.0f;
    float descent = 0.0f;
    float lineGap = 0.0f;
};

TextRenderer::TextRenderer()
    : m_impl(std::make_unique<Impl>())
{
}

TextRenderer::~TextRenderer() = default;

bool TextRenderer::IsReady() const
{
    return m_impl && m_impl->ready;
}

bool TextRenderer::Initialize(ID3D11Device *device, const char *ttfPath, float bakePixelHeight)
{
    if (!device || !ttfPath) return false;

    // TTF をメモリへ読む
    std::ifstream ifs(ttfPath, std::ios::binary);
    if (!ifs) return false;
    std::vector<unsigned char> ttf((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    if (ttf.empty()) return false;

    m_impl->bakePixel = bakePixelHeight;

    // ASCII を 1 チャンネルのアトラスへ焼く
    std::vector<unsigned char> bitmap(static_cast<size_t>(m_impl->atlasW) * m_impl->atlasH);
    stbtt_BakeFontBitmap(ttf.data(), 0, bakePixelHeight,
                         bitmap.data(), m_impl->atlasW, m_impl->atlasH,
                         Impl::kFirst, Impl::kCount, m_impl->chars);
    // 戻り値が負なら入りきらなかったが、入った分は焼けている。ASCII なら 512x512 / 48px で収まる

    // 縦方向のメトリクスを取得する
    stbtt_fontinfo info;
    if (stbtt_InitFont(&info, ttf.data(), stbtt_GetFontOffsetForIndex(ttf.data(), 0)))
    {
        int a = 0, d = 0, g = 0;
        stbtt_GetFontVMetrics(&info, &a, &d, &g);
        const float sc  = stbtt_ScaleForPixelHeight(&info, bakePixelHeight);
        m_impl->ascent  = a * sc;
        m_impl->descent = d * sc;
        m_impl->lineGap = g * sc;
    }

    // 1 チャンネルのカバレッジを RGBA（白 + カバレッジを α）へ変換する。
    //   Sprite はテクスチャ色 × 頂点色で描くので、RGB を白にして頂点色で着色する
    std::vector<uint32_t> rgba(static_cast<size_t>(m_impl->atlasW) * m_impl->atlasH);
    for (size_t i = 0; i < rgba.size(); ++i)
    {
        const uint32_t cov = bitmap[i];
        rgba[i]            = 0x00FFFFFFu | (cov << 24); // R8G8B8A8: R,G,B=255, A=カバレッジ
    }

    // テクスチャと SRV を作る
    D3D11_TEXTURE2D_DESC td = {};
    td.Width                = m_impl->atlasW;
    td.Height               = m_impl->atlasH;
    td.MipLevels            = 1;
    td.ArraySize            = 1;
    td.Format               = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count     = 1;
    td.Usage                = D3D11_USAGE_IMMUTABLE;
    td.BindFlags            = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA sd = {};
    sd.pSysMem                = rgba.data();
    sd.SysMemPitch            = m_impl->atlasW * 4;

    ComPtr<ID3D11Texture2D> tex;
    if (FAILED(device->CreateTexture2D(&td, &sd, tex.GetAddressOf()))) return false;

    ComPtr<ID3D11ShaderResourceView> srv;
    if (FAILED(device->CreateShaderResourceView(tex.Get(), nullptr, srv.GetAddressOf()))) return false;

    m_impl->atlas = std::make_unique<Sprite>(device, srv,
                                             static_cast<float>(m_impl->atlasW), static_cast<float>(m_impl->atlasH));
    m_impl->ready = true;
    return true;
}

void TextRenderer::DrawText(ID3D11DeviceContext *dc, const std::string &text,
                            float x, float y, float pixelSize, const DirectX::XMFLOAT4 &color)
{
    if (!IsReady() || !dc) return;

    const float scale = pixelSize / m_impl->bakePixel;
    float       penX  = x;
    float       baseY = y + m_impl->ascent * scale; // y は上端なので、ベースラインまで下げる

    for (unsigned char ch : text)
    {
        if (ch == '\n')
        {
            penX = x;
            baseY += LineHeight(pixelSize);
            continue;
        }
        if (ch < Impl::kFirst || ch >= Impl::kFirst + Impl::kCount) continue;

        const stbtt_bakedchar &b = m_impl->chars[ch - Impl::kFirst];

        // アトラス上の矩形（Sprite の切り抜きはピクセル指定）
        const float sx = static_cast<float>(b.x0);
        const float sy = static_cast<float>(b.y0);
        const float sw = static_cast<float>(b.x1 - b.x0);
        const float sh = static_cast<float>(b.y1 - b.y0);

        // 画面上の配置（焼いたときの高さ基準のオフセットを scale 倍する）
        const float gx = penX + b.xoff * scale;
        const float gy = baseY + b.yoff * scale;
        const float gw = sw * scale;
        const float gh = sh * scale;

        m_impl->atlas->Render(dc, gx, gy, 0.0f, gw, gh, sx, sy, sw, sh, 0.0f,
                              color.x, color.y, color.z, color.w);

        penX += b.xadvance * scale;
    }
}

float TextRenderer::MeasureWidth(const std::string &text, float pixelSize) const
{
    if (!m_impl->ready) return 0.0f;
    const float scale = pixelSize / m_impl->bakePixel;
    float       w = 0.0f, maxw = 0.0f;
    for (unsigned char ch : text)
    {
        if (ch == '\n')
        {
            maxw = (w > maxw) ? w : maxw;
            w    = 0.0f;
            continue;
        }
        if (ch < Impl::kFirst || ch >= Impl::kFirst + Impl::kCount) continue;
        w += m_impl->chars[ch - Impl::kFirst].xadvance * scale;
    }
    return (w > maxw) ? w : maxw;
}

float TextRenderer::LineHeight(float pixelSize) const
{
    const float scale = pixelSize / m_impl->bakePixel;
    // descent は負なので (ascent - descent) が文字の高さ。これに行間を足す
    return (m_impl->ascent - m_impl->descent + m_impl->lineGap) * scale;
}
