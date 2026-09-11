#pragma once

#include <string>

#include <wrl.h>
#include <d3d11.h>
#include <DirectXMath.h>

// スプライト
class Sprite
{
public:
    Sprite(ID3D11Device *device);
    Sprite(ID3D11Device *device, const char *filename);
    // 実行時生成テクスチャ（フォントアトラス等）を外部SRVから包む。
    // 描画パイプライン（VS/PS/頂点バッファ）は他コンストラクタと共通。
    Sprite(ID3D11Device *device, Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv, float width, float height);
    virtual ~Sprite() = default; // デストラクタを仮想関数にしておくのが安全

    // 頂点データ
    struct Vertex
    {
        DirectX::XMFLOAT3 position;
        DirectX::XMFLOAT4 color;
        DirectX::XMFLOAT2 texcoord;
    };

    // 描画実行
    void Render(ID3D11DeviceContext *dc,
                float dx, float dy,                // 左上位置
                float dz,                          // 奥行
                float dw, float dh,                // 幅、高さ
                float sx, float sy,                // 画像切り抜き位置
                float sw, float sh,                // 画像切り抜きサイズ
                float angle,                       // 角度
                float r, float g, float b, float a // 色
    ) const;

    // 描画実行（4隅それぞれに色を指定する本体）
    //   4隅で色を変えられる＝頂点補間でグラデーションになる。単色版は同じ色を4つ渡すだけ。
    //   色の並びは頂点と同じ 左上 / 右上 / 左下 / 右下。
    void Render(ID3D11DeviceContext *dc,
                float dx, float dy,                        // 左上位置
                float dz,                                  // 奥行
                float dw, float dh,                        // 幅、高さ
                float sx, float sy,                        // 画像切り抜き位置
                float sw, float sh,                        // 画像切り抜きサイズ
                float angle,                               // 角度
                const DirectX::XMFLOAT4 (&cornerColors)[4] // 左上/右上/左下/右下 の色
    ) const;

    // 描画実行（テクスチャ切り抜き指定なし）
    void Render(ID3D11DeviceContext *dc,
                float dx, float dy,                // 左上位置
                float dz,                          // 奥行
                float dw, float dh,                // 幅、高さ
                float angle,                       // 角度
                float r, float g, float b, float a // 色
    ) const;

    // Sprite.h の public: 部分に追加
    float GetWidth() const { return textureWidth; }
    float GetHeight() const { return textureHeight; }

    //void Textout(ID3D11DeviceContext*,
    //	std::string s,
    //	float x, float y, float w, float h,
    //	float r, float g, float b, float a
    //);

protected:
    Microsoft::WRL::ComPtr<ID3D11VertexShader> vertexShader;
    Microsoft::WRL::ComPtr<ID3D11PixelShader>  pixelShader;
    Microsoft::WRL::ComPtr<ID3D11InputLayout>  inputLayout;

    Microsoft::WRL::ComPtr<ID3D11Buffer>             vertexBuffer;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> shaderResourceView;
    Microsoft::WRL::ComPtr<ID3D11SamplerState>       samplerState;

    float textureWidth  = 0;
    float textureHeight = 0;
};
