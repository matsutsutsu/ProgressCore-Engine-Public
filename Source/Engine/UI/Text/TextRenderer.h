#pragma once
// ===========================================================================
// 概要: TTF フォントをアトラスに焼き、Sprite で 1 文字ずつ描くテキスト描画クラス TextRenderer。
//
// 責務:
//   - 半角英数字（' '〜'~'）は主のフォントから、起動時にまとめてアトラスへ焼く
//   - それ以外の文字（日本語など）は副のフォントから、初めて出てきたときに1文字ずつ別のアトラスへ足す
//   - 文字列（UTF-8）の描画、描画幅と行の高さの計算
//
// 備考:
//   - 文字のラスタライズは stb_truetype に任せ、描画は Sprite を使う。
//   - stb に依存する実装は Impl（pimpl）に隠し、ヘッダに stb を持ち込まない。
//   - 日本語は数千文字あり、全部を先に焼くとアトラスが巨大になる。画面に出た文字だけを焼けば、
//     メニューやヒントで使う数百文字で収まる。
//   - 副のフォントの文字は、主のフォントと同じベースラインにそろえて並べる。
//   - 副のアトラスが一杯になったら、入りきらない文字は飛ばす（落ちない）。
// ===========================================================================
#include <d3d11.h>
#include <DirectXMath.h>
#include <memory>
#include <string>

class TextRenderer
{
public:
    TextRenderer();
    ~TextRenderer();

    /**
     * @brief フォントを読み、半角英数字をアトラスへ焼く。
     * @param ttfPath         主のフォント（半角英数字）。
     * @param bakePixelHeight 焼くときの文字の高さ（px）。描くときはこれを拡大縮小する
     * @param fallbackTtfPath 副のフォント（日本語など、半角英数字以外）。nullptr なら主のフォントを使う
     * @return 成功で true
     */
    bool Initialize(ID3D11Device *device, const char *ttfPath, float bakePixelHeight = 48.0f,
                    const char *fallbackTtfPath = nullptr);
    bool IsReady() const;

    // 文字列（UTF-8）を (x, y) を左上として、指定の高さ（px）で描く
    void DrawText(ID3D11DeviceContext *dc, const std::string &text,
                  float x, float y, float pixelSize, const DirectX::XMFLOAT4 &color);

    // 文字列（UTF-8）の描画幅（px）。複数行なら最も長い行の幅
    float MeasureWidth(const std::string &text, float pixelSize) const;

    // 1 行の高さ（px）
    float LineHeight(float pixelSize) const;

    // UTF-8 の文字列の文字数（バイト数ではない）。日本語は1文字が3バイトなので、打ち込みの演出で文字を数えるのに使う
    static size_t Utf8Length(const std::string &text);

    // UTF-8 の文字列の先頭から count 文字を返す（文字の途中で切らない）
    static std::string Utf8Prefix(const std::string &text, size_t count);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
