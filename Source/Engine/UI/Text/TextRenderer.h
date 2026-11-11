#pragma once
// ===========================================================================
// 概要: TTF フォントを1枚の画像（アトラス）に焼き、Sprite で1文字ずつ描くテキスト描画
//
// 責務:
//   - 半角英数字は、主のフォントから初期化時にまとめて焼く
//   - それ以外（日本語など）は、副のフォントから初めて描くときに1文字ずつ別のアトラスへ焼き足す
//   - UTF-8 の文字列を描き、描画幅と行の高さを計算する
//
// 備考: 日本語は数千字あり、全部を先に焼くとアトラスが巨大になる。画面に出た文字だけなら数百字で収まる
//       副のアトラスが一杯になったら、入りきらない文字は描かずに飛ばす
//       stb_truetype に依存する実装は Impl（pimpl）に隠し、ヘッダに stb を持ち込まない
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

    // フォントを読み、半角英数字をアトラスへ焼く。副のフォント（日本語など）は読み込みと空のアトラスの用意だけする
    // bakePixelHeight は焼くときの文字の高さ（px）。副のフォントが使えなくても、主のフォントが使えれば true
    bool Initialize(ID3D11Device *device, const char *ttfPath, float bakePixelHeight = 48.0f,
                    const char *fallbackTtfPath = nullptr);
    bool IsReady() const;

    // UTF-8 の文字列を、(x, y) を1行目の左上（px）にして描く。\n で次の行へ移る
    // 初めて描く副のフォントの文字はここでアトラスへ書くので、メインスレッドから呼ぶ
    void DrawText(ID3D11DeviceContext *dc, const std::string &text,
                  float x, float y, float pixelSize, const DirectX::XMFLOAT4 &color);

    // 文字列（UTF-8）を pixelSize の高さで描いたときの幅（px）。複数行なら最も長い行の幅。アトラスには焼かない
    float MeasureWidth(const std::string &text, float pixelSize) const;

    // pixelSize の高さで描いたときの1行の高さ（px、行間を含む）
    float LineHeight(float pixelSize) const;

    // UTF-8 の文字列の文字数（バイト数ではない）。打ち込みの演出で文字を数えるのに使う
    static size_t Utf8Length(const std::string &text);

    // UTF-8 の文字列の先頭から count 文字を返す（文字の途中で切らない）
    static std::string Utf8Prefix(const std::string &text, size_t count);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
