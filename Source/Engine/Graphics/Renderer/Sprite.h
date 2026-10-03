#pragma once
// ===========================================================================
// 概要: テクスチャ1枚を、画面のピクセル座標で四角形として描くクラス。
//
// 責務:
//   - 画像ファイル・白のダミー・外から渡された SRV のどれかをテクスチャとして持つ
//   - 位置・サイズ・切り抜き範囲・回転・色（4隅それぞれも可）を指定して描く
//
// 備考: 座標は今のビューポートの大きさで NDC に直す。深度テストやブレンドのステートは
//       呼び出し側で設定しておく。
// ===========================================================================
#include <string>

#include <wrl.h>
#include <d3d11.h>
#include <DirectXMath.h>

class Sprite
{
public:
    // 白 1x1 のダミーテクスチャで作る
    Sprite(ID3D11Device *device);
    // filename の画像を読み込んで作る（nullptr ならダミー）
    Sprite(ID3D11Device *device, const char *filename);
    // 実行中に作ったテクスチャ（フォントアトラスなど）を使う。width / height はテクスチャのサイズ（px）
    Sprite(ID3D11Device *device, Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv, float width, float height);
    virtual ~Sprite() = default;

    struct Vertex
    {
        DirectX::XMFLOAT3 position; // NDC
        DirectX::XMFLOAT4 color;
        DirectX::XMFLOAT2 texcoord;
    };

    // 位置・サイズ・切り抜きはピクセル、angle は度。回転の中心は四角形の中心
    void Render(ID3D11DeviceContext *dc,
                float dx, float dy,                // 左上の位置
                float dz,                          // 奥行き（NDC の z）
                float dw, float dh,                // 幅・高さ
                float sx, float sy,                // テクスチャから切り抜く左上
                float sw, float sh,                // 切り抜くサイズ
                float angle,                       // 回転（度）
                float r, float g, float b, float a // 色
    ) const;

    /**
     * @brief 4隅に別々の色を指定して描く（間は補間されてグラデーションになる）。上の単色版もこれを呼ぶ。
     *
     * 位置・サイズ・切り抜き・回転の引数は単色版と同じ。
     *
     * @param cornerColors 4隅の色。並びは頂点と同じ 左上 / 右上 / 左下 / 右下。
     */
    void Render(ID3D11DeviceContext *dc,
                float dx, float dy,
                float dz,
                float dw, float dh,
                float sx, float sy,
                float sw, float sh,
                float angle,
                const DirectX::XMFLOAT4 (&cornerColors)[4]) const;

    // テクスチャ全体を描く
    void Render(ID3D11DeviceContext *dc,
                float dx, float dy,
                float dz,
                float dw, float dh,
                float angle,
                float r, float g, float b, float a) const;

    float GetWidth() const { return textureWidth; }   // テクスチャの幅（px）
    float GetHeight() const { return textureHeight; } // テクスチャの高さ（px）

protected:
    Microsoft::WRL::ComPtr<ID3D11VertexShader> vertexShader;
    Microsoft::WRL::ComPtr<ID3D11PixelShader>  pixelShader;
    Microsoft::WRL::ComPtr<ID3D11InputLayout>  inputLayout;

    Microsoft::WRL::ComPtr<ID3D11Buffer>             vertexBuffer; // 4頂点（毎回 Map で書き換える）
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> shaderResourceView;
    Microsoft::WRL::ComPtr<ID3D11SamplerState>       samplerState;

    float textureWidth  = 0; // px
    float textureHeight = 0; // px
};
