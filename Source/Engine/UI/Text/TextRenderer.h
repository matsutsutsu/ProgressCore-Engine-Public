#pragma once
// ===========================================================================
// 概要: TTF フォントをアトラスに焼き、Sprite で 1 文字ずつ描くテキスト描画クラス TextRenderer。
//
// 責務:
//   - TTF の ASCII 範囲をアトラステクスチャへ焼く
//   - 文字列の描画、描画幅と行の高さの計算
//
// 備考:
//   - 文字のラスタライズは stb_truetype に任せ、描画は Sprite を使う。
//   - stb に依存する実装は Impl（pimpl）に隠し、ヘッダに stb を持ち込まない。
//   - 描けるのは ASCII（' '〜'~'）だけ。範囲外の文字は飛ばす。
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

    // TTF を読み、ASCII 範囲をアトラスへ焼く。成功で true
    bool Initialize(ID3D11Device *device, const char *ttfPath, float bakePixelHeight = 48.0f);
    bool IsReady() const;

    // 文字列を (x, y) を左上として、指定の高さ（px）で描く
    void DrawText(ID3D11DeviceContext *dc, const std::string &text,
                  float x, float y, float pixelSize, const DirectX::XMFLOAT4 &color);

    // 文字列の描画幅（px）。複数行なら最も長い行の幅
    float MeasureWidth(const std::string &text, float pixelSize) const;

    // 1 行の高さ（px）
    float LineHeight(float pixelSize) const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
