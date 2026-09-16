#pragma once
// stb_truetype でTTFをアトラスに焼き、既存Spriteでグリフ矩形を描くテキスト描画。
//
// 設計方針（基礎=ライブラリ / 統合=自作）:
//   - 文字→ビットマップのラスタライズは stb_truetype に任せる（一番面倒な部分）。
//   - アトラス化・グリフ矩形の描画は既存 Sprite パイプラインへ統合（自作の薄い層）。
//   - stb 依存の実装詳細は pimpl(Impl) に隠し、ヘッダに stb を漏らさない。
#include <d3d11.h>
#include <DirectXMath.h>
#include <memory>
#include <string>

class TextRenderer
{
public:
    TextRenderer();
    ~TextRenderer();

    // TTFを読み、ASCII範囲をアトラスへ焼く。成功でtrue。
    bool Initialize(ID3D11Device *device, const char *ttfPath, float bakePixelHeight = 48.0f);
    bool IsReady() const;

    // 文字列を (x,y) 左上基準・指定ピクセル高で描画。
    void DrawText(ID3D11DeviceContext *dc, const std::string &text,
                  float x, float y, float pixelSize, const DirectX::XMFLOAT4 &color);

    // 文字列の描画幅(px)を計算（レイアウト用）。
    float MeasureWidth(const std::string &text, float pixelSize) const;
    // 1行の高さ(px)。
    float LineHeight(float pixelSize) const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
