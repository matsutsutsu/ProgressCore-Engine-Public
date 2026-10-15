#pragma once
// ===========================================================================
// 概要: TTF フォントを1枚の画像（アトラス）に焼き、Sprite で1文字ずつ描くテキスト描画クラス TextRenderer。
//
// 責務:
//   - 半角英数字（' '〜'~'）を、主のフォントから初期化時にまとめてアトラスへ焼く
//   - それ以外の文字（日本語など）を、副のフォントから初めて描くときに1文字ずつ別のアトラスへ焼き足す
//   - UTF-8 の文字列を描き、描画幅と行の高さを計算する
//
// 備考:
//   文字の画像化は stb_truetype に任せ、描画は Sprite を使う。
//   stb に依存する実装は Impl（pimpl）に隠し、ヘッダに stb を持ち込まない。
//
//   日本語は数千文字あり、全部を先に焼くとアトラスが巨大になる。
//   画面に出た文字だけを焼けば、メニューやヒントで使う数百文字で収まる。
//
//   副のフォントの文字は、主のフォントと同じベースラインにそろえて並べる。
//
//   副のアトラスが一杯になったら、入りきらない文字は描かずに飛ばす（落ちない）。
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
     * @brief フォントを読み、半角英数字をアトラスへ焼く。副のフォントは読み込みと空のアトラスの用意だけを行う。
     * @param device          アトラスのテクスチャを作るデバイス。
     * @param ttfPath         主のフォント（半角英数字）のパス。
     * @param bakePixelHeight 焼くときの文字の高さ（px）。描くときはこれを拡大縮小する。
     * @param fallbackTtfPath 副のフォント（日本語など、半角英数字以外）のパス。nullptr や読めないときは主のフォントを使う。
     * @return 主のフォントを焼けたら true。副のフォントが使えなくても、主のフォントが使えれば true。
     */
    bool Initialize(ID3D11Device *device, const char *ttfPath, float bakePixelHeight = 48.0f,
                    const char *fallbackTtfPath = nullptr);
    bool IsReady() const;

    /**
     * @brief UTF-8 の文字列を描く。改行（\n）で次の行へ移る。
     * @param dc        描画に使うデバイスコンテキスト。初めて描く副のフォントの文字は、これでアトラスへ書き込む。
     * @param text      描く文字列（UTF-8）。
     * @param x         1行目の左端（px、画面の左上が原点）。
     * @param y         1行目の上端（px）。
     * @param pixelSize 文字の高さ（px）。
     * @param color     文字の色（RGBA、0〜1）。
     * @note アトラスへの書き込みがあるので、メインスレッドから呼ぶ。
     */
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
