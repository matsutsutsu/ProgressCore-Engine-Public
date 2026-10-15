// stb_truetype の実装は、この翻訳単位でだけ展開する。
// STBTT_STATIC で関数を static にし、ImGui が内部に持つ同名の stb 実装とリンク時に衝突しないようにする。
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "stb_truetype.h"

#include "TextRenderer.h"
#include "Engine/Graphics/Renderer/Sprite.h"

#include <wrl.h>
#include <cstdint>
#include <fstream>
#include <mutex>
#include <unordered_map>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace
{
    /**
     * @brief ファイルを丸ごとバイト列として読む。
     * @param path 読むファイルのパス。
     * @return ファイルの中身。開けなかったときは空。
     */
    std::vector<unsigned char> ReadFile(const char *path)
    {
        std::ifstream ifs(path, std::ios::binary);
        if (!ifs)
        {
            return {};
        }
        return std::vector<unsigned char>((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    }

    /**
     * @brief UTF-8 の文字列から1文字（コードポイント）を取り出し、i を次の文字の先頭へ進める。
     * @param s 読む文字列（UTF-8）。
     * @param i 読み始める位置（バイト）。読んだ分だけ進める。s.size() 未満であること。
     * @return コードポイント。壊れたバイト列なら 0xFFFD（置き換え文字）を返し、1バイトだけ進める。
     */
    uint32_t NextCodepoint(const std::string &s, size_t &i)
    {
        const auto c0 = static_cast<unsigned char>(s[i]);
        if (c0 < 0x80)
        {
            ++i;
            return c0;
        }

        // 先頭バイトの上位ビットから、続くバイトの数を決める。
        int      extra = 0;
        uint32_t cp    = 0;
        if ((c0 & 0xE0) == 0xC0)
        {
            extra = 1;
            cp    = c0 & 0x1F;
        }
        else if ((c0 & 0xF0) == 0xE0)
        {
            extra = 2;
            cp    = c0 & 0x0F;
        }
        else if ((c0 & 0xF8) == 0xF0)
        {
            extra = 3;
            cp    = c0 & 0x07;
        }
        else
        {
            ++i;
            return 0xFFFD;
        }

        // 続くバイトが足りない（文字列が文字の途中で切れている）。
        if (i + extra >= s.size())
        {
            ++i;
            return 0xFFFD;
        }
        for (int k = 1; k <= extra; ++k)
        {
            const auto c = static_cast<unsigned char>(s[i + k]);
            if ((c & 0xC0) != 0x80)
            {
                ++i;
                return 0xFFFD;
            }
            cp = (cp << 6) | (c & 0x3F);
        }
        i += 1 + extra;
        return cp;
    }
} // namespace

// stb に依存する状態をまとめて持つ（ヘッダに stb を持ち込まないため）
struct TextRenderer::Impl
{
    static constexpr int kFirst = 32; // 主のアトラスに焼く最初の文字（' '）
    static constexpr int kCount = 96; // 主のアトラスに焼く文字数（' '〜'~'）

    bool  ready     = false; // Initialize が成功したか
    float bakePixel = 48.0f; // 焼いたときの文字の高さ（px）

    // --- 主のフォント（半角英数字。初期化時にまとめて焼く） ---
    int                     atlasW        = 512; // 主のアトラスの幅（px）
    int                     atlasH        = 512; // 主のアトラスの高さ（px）
    stbtt_bakedchar         chars[kCount] = {};  // 各文字のアトラス上の位置と送り幅
    std::unique_ptr<Sprite> atlas;               // 主のアトラスを描く Sprite

    // --- 縦方向のメトリクス（bakePixel の高さでの px） ---
    float ascent  = 0.0f; // ベースラインから上端まで
    float descent = 0.0f; // ベースラインから下端まで（負）
    float lineGap = 0.0f; // 行と行の間

    // --- 副のフォント（それ以外の文字。初めて描くときに1文字ずつ焼く） ---
    std::vector<unsigned char> fallbackTtf;          // フォントファイルの中身。fallbackInfo が中を指すので持ち続ける
    stbtt_fontinfo             fallbackInfo{};       // stb のフォント情報
    bool                       hasFallback   = false; // 副のフォントとアトラスが使えるか
    float                      fallbackScale = 0.0f;  // フォントの単位を bakePixel の高さの px にする倍率

    // 副のフォントの1文字。メトリクスは測るときに、アトラス上の位置は初めて描くときに決める
    struct Glyph
    {
        float xadvance = 0.0f; // 次の文字までの幅（bakePixel 基準の px）
        float xoff = 0.0f, yoff = 0.0f; // ベースラインからの左上のずれ（bakePixel 基準の px）
        int   w = 0, h = 0;    // ビットマップの大きさ（px）
        int   ax = 0, ay = 0;  // アトラス上の左上（px）
        bool  baked = false;   // アトラスへ焼いたか
        bool  fits  = true;    // アトラスに入ったか（入らなければ描かない）
    };
    std::unordered_map<uint32_t, Glyph> glyphs;     // コードポイントごとの文字の情報
    std::mutex                          glyphMutex; // glyphs と副のアトラスを守る（幅の計測は別のスレッドから呼ばれることがある）

    static constexpr int    kDynSize = 1024;                    // 副のアトラスの一辺（px）。48px の文字なら約 400 文字入る
    ComPtr<ID3D11Texture2D> dynTex;                             // 副のアトラスのテクスチャ（部分的に書き換える）
    std::unique_ptr<Sprite> dynAtlas;                           // 副のアトラスを描く Sprite
    int                     shelfX = 1, shelfY = 1, shelfH = 0; // 棚詰めの次の書き込み位置と、今の棚の高さ（px）

    /**
     * @brief 副のフォントの文字の情報を引く。まだ無ければメトリクスを測って登録する。
     * @param cp 引く文字のコードポイント。
     * @return 文字の情報。glyphs が所有する。アトラスにはまだ焼いていないことがある。
     * @note 呼ぶ側で glyphMutex を取ること。
     */
    Glyph &FindGlyph(uint32_t cp)
    {
        auto it = glyphs.find(cp);
        if (it != glyphs.end())
        {
            return it->second;
        }

        Glyph g;
        int   advance = 0, lsb = 0;
        stbtt_GetCodepointHMetrics(&fallbackInfo, static_cast<int>(cp), &advance, &lsb);
        g.xadvance = advance * fallbackScale;

        int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        stbtt_GetCodepointBitmapBox(&fallbackInfo, static_cast<int>(cp), fallbackScale, fallbackScale, &x0, &y0, &x1, &y1);
        g.xoff = static_cast<float>(x0);
        g.yoff = static_cast<float>(y0);
        g.w    = x1 - x0;
        g.h    = y1 - y0;
        return glyphs.emplace(cp, g).first->second;
    }

    /**
     * @brief 文字を副のアトラスへ焼く。焼き済みなら何もしない。
     * @param dc アトラスのテクスチャへ書き込むデバイスコンテキスト。
     * @param cp 焼く文字のコードポイント。
     * @param g  焼く文字の情報（FindGlyph で引いたもの）。焼いた位置と、入ったかどうかを書き込む。
     * @note 呼ぶ側で glyphMutex を取ること。
     * @note アトラスが一杯なら g.fits を false にして諦める。その文字は以後描かない。
     */
    void BakeGlyph(ID3D11DeviceContext *dc, uint32_t cp, Glyph &g)
    {
        if (g.baked)
        {
            return;
        }
        g.baked = true;

        // 空白など、絵の無い文字はアトラスに場所を取らない。
        if (g.w <= 0 || g.h <= 0)
        {
            return;
        }

        // 棚詰め: 左から順に並べ、今の段に入らなければ次の段へ移る。文字の間には 1px の余白を空ける。
        if (shelfX + g.w + 1 > kDynSize)
        {
            shelfX = 1;
            shelfY += shelfH + 1;
            shelfH = 0;
        }
        if (shelfY + g.h + 1 > kDynSize)
        {
            g.fits = false;
            return;
        }
        g.ax = shelfX;
        g.ay = shelfY;
        shelfX += g.w + 1;
        shelfH = (g.h > shelfH) ? g.h : shelfH;

        // 1チャンネル（覆う割合）で焼き、RGB を白・α を覆う割合にした RGBA へ直す。
        // アトラスは、この文字の部分だけを書き換える。
        std::vector<unsigned char> cov(static_cast<size_t>(g.w) * g.h);
        stbtt_MakeCodepointBitmap(&fallbackInfo, cov.data(), g.w, g.h, g.w, fallbackScale, fallbackScale,
                                  static_cast<int>(cp));
        std::vector<uint32_t> rgba(cov.size());
        for (size_t i = 0; i < cov.size(); ++i)
        {
            rgba[i] = 0x00FFFFFFu | (static_cast<uint32_t>(cov[i]) << 24);
        }

        D3D11_BOX box = {};
        box.left      = static_cast<UINT>(g.ax);
        box.top       = static_cast<UINT>(g.ay);
        box.right     = static_cast<UINT>(g.ax + g.w);
        box.bottom    = static_cast<UINT>(g.ay + g.h);
        box.back      = 1;
        dc->UpdateSubresource(dynTex.Get(), 0, &box, rgba.data(), static_cast<UINT>(g.w * 4), 0);
    }
};

// ---------------------------------------------------------------------------
// 初期化
// ---------------------------------------------------------------------------

TextRenderer::TextRenderer()
    : m_impl(std::make_unique<Impl>())
{
}

TextRenderer::~TextRenderer() = default;

bool TextRenderer::IsReady() const
{
    return m_impl && m_impl->ready;
}

bool TextRenderer::Initialize(ID3D11Device *device, const char *ttfPath, float bakePixelHeight, const char *fallbackTtfPath)
{
    // 処理フロー:
    // 1. 主のフォントを読む
    // 2. 半角英数字をアトラスへ焼く
    // 3. 縦方向のメトリクスを取る
    // 4. アトラスをテクスチャにする
    // 5. 副のフォントを読み、空のアトラスを用意する

    // ---------------------------------------------------------
    // 1. 主のフォント
    // ---------------------------------------------------------
    if (!device || !ttfPath)
    {
        return false;
    }

    const std::vector<unsigned char> ttf = ReadFile(ttfPath);
    if (ttf.empty())
    {
        return false;
    }

    m_impl->bakePixel = bakePixelHeight;

    // ---------------------------------------------------------
    // 2. 半角英数字を焼く
    // ---------------------------------------------------------

    // 1チャンネル（覆う割合）の画像へ焼く。
    // 戻り値が負なら入りきらなかったことを表すが、入った分は焼けている。半角英数字なら 512x512・48px で収まる。
    std::vector<unsigned char> bitmap(static_cast<size_t>(m_impl->atlasW) * m_impl->atlasH);
    stbtt_BakeFontBitmap(ttf.data(), 0, bakePixelHeight,
                         bitmap.data(), m_impl->atlasW, m_impl->atlasH,
                         Impl::kFirst, Impl::kCount, m_impl->chars);

    // ---------------------------------------------------------
    // 3. 縦方向のメトリクス
    // ---------------------------------------------------------
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

    // ---------------------------------------------------------
    // 4. テクスチャ
    // ---------------------------------------------------------

    // 覆う割合を RGBA（RGB は白、α に覆う割合）へ直す。
    // Sprite はテクスチャの色 × 頂点の色で描くので、RGB を白にしておけば頂点の色がそのまま文字の色になる。
    std::vector<uint32_t> rgba(static_cast<size_t>(m_impl->atlasW) * m_impl->atlasH);
    for (size_t i = 0; i < rgba.size(); ++i)
    {
        const uint32_t cov = bitmap[i];
        rgba[i]            = 0x00FFFFFFu | (cov << 24); // R8G8B8A8: R,G,B=255, A=カバレッジ
    }

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
    if (FAILED(device->CreateTexture2D(&td, &sd, tex.GetAddressOf())))
    {
        return false;
    }

    ComPtr<ID3D11ShaderResourceView> srv;
    if (FAILED(device->CreateShaderResourceView(tex.Get(), nullptr, srv.GetAddressOf())))
    {
        return false;
    }

    m_impl->atlas = std::make_unique<Sprite>(device, srv,
                                             static_cast<float>(m_impl->atlasW), static_cast<float>(m_impl->atlasH));

    // ---------------------------------------------------------
    // 5. 副のフォント
    // ---------------------------------------------------------

    // ここでは読み込みと空のアトラスの用意だけを行う。文字は DrawText で初めて描くときに焼く。

    // 副のフォントが指定されていない・読めないときは、主のフォントで代用する。
    m_impl->fallbackTtf = fallbackTtfPath ? ReadFile(fallbackTtfPath) : ttf;
    if (m_impl->fallbackTtf.empty())
    {
        m_impl->fallbackTtf = ttf;
    }
    if (stbtt_InitFont(&m_impl->fallbackInfo, m_impl->fallbackTtf.data(),
                       stbtt_GetFontOffsetForIndex(m_impl->fallbackTtf.data(), 0)))
    {
        m_impl->fallbackScale = stbtt_ScaleForPixelHeight(&m_impl->fallbackInfo, bakePixelHeight);

        D3D11_TEXTURE2D_DESC dd = td;
        dd.Width                = Impl::kDynSize;
        dd.Height               = Impl::kDynSize;
        dd.Usage                = D3D11_USAGE_DEFAULT; // UpdateSubresource で部分的に書き換える

        std::vector<uint32_t>  clear(static_cast<size_t>(Impl::kDynSize) * Impl::kDynSize, 0x00FFFFFFu); // 白で透明
        D3D11_SUBRESOURCE_DATA dsd = {};
        dsd.pSysMem                = clear.data();
        dsd.SysMemPitch            = Impl::kDynSize * 4;

        ComPtr<ID3D11ShaderResourceView> dynSrv;
        if (SUCCEEDED(device->CreateTexture2D(&dd, &dsd, m_impl->dynTex.GetAddressOf())) &&
            SUCCEEDED(device->CreateShaderResourceView(m_impl->dynTex.Get(), nullptr, dynSrv.GetAddressOf())))
        {
            m_impl->dynAtlas    = std::make_unique<Sprite>(device, dynSrv, static_cast<float>(Impl::kDynSize),
                                                           static_cast<float>(Impl::kDynSize));
            m_impl->hasFallback = true;
        }
    }

    m_impl->ready = true;
    return true;
}

// ---------------------------------------------------------------------------
// 描画と計測
// ---------------------------------------------------------------------------

void TextRenderer::DrawText(ID3D11DeviceContext *dc, const std::string &text,
                            float x, float y, float pixelSize, const DirectX::XMFLOAT4 &color)
{
    if (!IsReady() || !dc)
    {
        return;
    }

    const float scale = pixelSize / m_impl->bakePixel; // 焼いた高さから描く高さへの倍率
    float       penX  = x;                             // 次の文字を置く横位置
    float       baseY = y + m_impl->ascent * scale;    // ベースラインの高さ（y は上端なので、ascent の分だけ下げる）

    for (size_t i = 0; i < text.size();)
    {
        const uint32_t cp = NextCodepoint(text, i);
        if (cp == '\n')
        {
            penX = x;
            baseY += LineHeight(pixelSize);
            continue;
        }

        // --- 半角英数字: 主のアトラスから ---
        if (cp >= static_cast<uint32_t>(Impl::kFirst) && cp < static_cast<uint32_t>(Impl::kFirst + Impl::kCount))
        {
            const stbtt_bakedchar &b = m_impl->chars[cp - Impl::kFirst];

            // アトラス上の切り抜く四角形（Sprite の切り抜きはピクセルで指定する）
            const float sx = static_cast<float>(b.x0);
            const float sy = static_cast<float>(b.y0);
            const float sw = static_cast<float>(b.x1 - b.x0);
            const float sh = static_cast<float>(b.y1 - b.y0);

            // 焼いた高さを基準にしたずれと大きさを、描く高さへ scale 倍して置く。
            m_impl->atlas->Render(dc, penX + b.xoff * scale, baseY + b.yoff * scale, 0.0f, sw * scale, sh * scale,
                                  sx, sy, sw, sh, 0.0f, color.x, color.y, color.z, color.w);
            penX += b.xadvance * scale;
            continue;
        }

        // --- それ以外（日本語など）: 副のアトラスから。初めて描く文字はここで焼く ---

        // 0x20 未満は制御文字なので描かない。
        if (!m_impl->hasFallback || cp < 0x20)
        {
            continue;
        }
        std::lock_guard<std::mutex> lk(m_impl->glyphMutex);
        Impl::Glyph                &g = m_impl->FindGlyph(cp);
        m_impl->BakeGlyph(dc, cp, g);
        if (g.fits && g.w > 0 && g.h > 0)
        {
            m_impl->dynAtlas->Render(dc, penX + g.xoff * scale, baseY + g.yoff * scale, 0.0f, g.w * scale, g.h * scale,
                                     static_cast<float>(g.ax), static_cast<float>(g.ay), static_cast<float>(g.w),
                                     static_cast<float>(g.h), 0.0f, color.x, color.y, color.z, color.w);
        }
        penX += g.xadvance * scale;
    }
}

float TextRenderer::MeasureWidth(const std::string &text, float pixelSize) const
{
    if (!m_impl->ready)
    {
        return 0.0f;
    }
    const float scale = pixelSize / m_impl->bakePixel;
    float       w = 0.0f, maxw = 0.0f;
    for (size_t i = 0; i < text.size();)
    {
        const uint32_t cp = NextCodepoint(text, i);
        if (cp == '\n')
        {
            maxw = (w > maxw) ? w : maxw;
            w    = 0.0f;
            continue;
        }
        if (cp >= static_cast<uint32_t>(Impl::kFirst) && cp < static_cast<uint32_t>(Impl::kFirst + Impl::kCount))
        {
            w += m_impl->chars[cp - Impl::kFirst].xadvance * scale;
        }
        else if (m_impl->hasFallback && cp >= 0x20)
        {
            // 幅を測るだけなので、アトラスには焼かない。
            // 焼くには DeviceContext が要り、この関数は別のスレッドから呼ばれることがある。
            std::lock_guard<std::mutex> lk(m_impl->glyphMutex);
            w += m_impl->FindGlyph(cp).xadvance * scale;
        }
    }
    return (w > maxw) ? w : maxw;
}

// ---------------------------------------------------------------------------
// 補助
// ---------------------------------------------------------------------------

size_t TextRenderer::Utf8Length(const std::string &text)
{
    size_t n = 0;
    for (size_t i = 0; i < text.size(); ++n)
    {
        NextCodepoint(text, i);
    }
    return n;
}

std::string TextRenderer::Utf8Prefix(const std::string &text, size_t count)
{
    size_t i = 0;
    for (size_t n = 0; n < count && i < text.size(); ++n)
    {
        NextCodepoint(text, i);
    }
    return text.substr(0, i);
}

float TextRenderer::LineHeight(float pixelSize) const
{
    const float scale = pixelSize / m_impl->bakePixel;

    // descent は負なので、ascent - descent が文字の高さになる。これに行間を足す。
    return (m_impl->ascent - m_impl->descent + m_impl->lineGap) * scale;
}
