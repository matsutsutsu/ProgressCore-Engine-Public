#include "Sprite.h"
#include "Engine/Core/Math/Misc.h"
#include "Engine/Graphics/RHI/GpuResourceUtils.h"

Sprite::Sprite(ID3D11Device *device)
    : Sprite(device, nullptr)
{
}



Sprite::Sprite(ID3D11Device *device, const char *filename)
{
    HRESULT hr = S_OK;

    // 4頂点。描くたびに書き直すので DYNAMIC
    {
        D3D11_BUFFER_DESC buffer_desc   = {};
        buffer_desc.ByteWidth           = sizeof(Vertex) * 4;
        buffer_desc.Usage               = D3D11_USAGE_DYNAMIC;
        buffer_desc.BindFlags           = D3D11_BIND_VERTEX_BUFFER;
        buffer_desc.CPUAccessFlags      = D3D11_CPU_ACCESS_WRITE;
        buffer_desc.MiscFlags           = 0;
        buffer_desc.StructureByteStride = 0;
        hr = device->CreateBuffer(&buffer_desc, nullptr, vertexBuffer.GetAddressOf());
        _ASSERT_EXPR(SUCCEEDED(hr), HRTrace(hr));
    }

    {
        D3D11_INPUT_ELEMENT_DESC inputElementDesc[] =
            {
                {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
                {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
                {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
            };
        hr = GpuResourceUtils::LoadVertexShader(
            device,
            "Assets/Shader/SpriteVS.cso",
            inputElementDesc,
            ARRAYSIZE(inputElementDesc),
            inputLayout.GetAddressOf(),
            vertexShader.GetAddressOf());
        _ASSERT_EXPR(SUCCEEDED(hr), HRTrace(hr));
    }

    {
        hr = GpuResourceUtils::LoadPixelShader(
            device,
            "Assets/Shader/SpritePS.cso",
            pixelShader.GetAddressOf());
        _ASSERT_EXPR(SUCCEEDED(hr), HRTrace(hr));
    }

    // SpritePS は s0 でサンプリングするので、Render で必ずバインドする
    // （しないと D3D11 のデバッグレイヤーが DEVICE_DRAW_SAMPLER_NOT_SET の警告を出す）
    {
        D3D11_SAMPLER_DESC desc = {};
        desc.Filter             = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        desc.AddressU           = D3D11_TEXTURE_ADDRESS_CLAMP;
        desc.AddressV           = D3D11_TEXTURE_ADDRESS_CLAMP;
        desc.AddressW           = D3D11_TEXTURE_ADDRESS_CLAMP;
        desc.ComparisonFunc     = D3D11_COMPARISON_NEVER;
        desc.MinLOD             = 0;
        desc.MaxLOD             = D3D11_FLOAT32_MAX;
        hr                      = device->CreateSamplerState(&desc, samplerState.GetAddressOf());
        _ASSERT_EXPR(SUCCEEDED(hr), HRTrace(hr));
    }

    if (filename != nullptr)
    {
        D3D11_TEXTURE2D_DESC desc;
        hr = GpuResourceUtils::LoadTexture(device, filename, shaderResourceView.GetAddressOf(), &desc);
        _ASSERT_EXPR(SUCCEEDED(hr), HRTrace(hr));

        textureWidth  = static_cast<float>(desc.Width);
        textureHeight = static_cast<float>(desc.Height);
    }
    else
    {
        D3D11_TEXTURE2D_DESC desc;
        hr = GpuResourceUtils::CreateDummyTexture(device, 0xFFFFFFFF, shaderResourceView.GetAddressOf(),
                                                  &desc);
        _ASSERT_EXPR(SUCCEEDED(hr), HRTrace(hr));

        textureWidth  = static_cast<float>(desc.Width);
        textureHeight = static_cast<float>(desc.Height);
    }
}



Sprite::Sprite(ID3D11Device *device, Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv, float width, float height)
    : Sprite(device)
{
    // シェーダーと頂点バッファは Sprite(device) で作り、ダミーのテクスチャを srv に差し替える

    shaderResourceView = srv;
    textureWidth       = width;
    textureHeight      = height;
}



void Sprite::Render(ID3D11DeviceContext *dc,
                    float dx, float dy,
                    float dz,
                    float dw, float dh,
                    float sx, float sy,
                    float sw, float sh,
                    float angle,
                    const DirectX::XMFLOAT4 (&cornerColors)[4]) const
{
    DirectX::XMFLOAT2 positions[] = {
        DirectX::XMFLOAT2(dx, dy),           // 左上
        DirectX::XMFLOAT2(dx + dw, dy),      // 右上
        DirectX::XMFLOAT2(dx, dy + dh),      // 左下
        DirectX::XMFLOAT2(dx + dw, dy + dh), // 右下
    };

    DirectX::XMFLOAT2 texcoords[] = {
        DirectX::XMFLOAT2(sx, sy),           // 左上
        DirectX::XMFLOAT2(sx + sw, sy),      // 右上
        DirectX::XMFLOAT2(sx, sy + sh),      // 左下
        DirectX::XMFLOAT2(sx + sw, sy + sh), // 右下
    };

    // 四角形の中心で回すため、中心が原点に来るように動かしてから回し、元に戻す
    float mx = dx + dw * 0.5f;
    float my = dy + dh * 0.5f;
    for (auto &p : positions)
    {
        p.x -= mx;
        p.y -= my;
    }

    float theta = DirectX::XMConvertToRadians(angle);
    float c     = cosf(theta);
    float s     = sinf(theta);
    for (auto &p : positions)
    {
        DirectX::XMFLOAT2 r = p;
        p.x                 = c * r.x + -s * r.y;
        p.y                 = s * r.x + c * r.y;
    }

    for (auto &p : positions)
    {
        p.x += mx;
        p.y += my;
    }

    // 今のビューポートの大きさを画面の大きさとして使う
    D3D11_VIEWPORT viewport;
    UINT           numViewports = 1;
    dc->RSGetViewports(&numViewports, &viewport);
    float screenWidth  = viewport.Width;
    float screenHeight = viewport.Height;

    // ピクセル座標を NDC（-1〜1、y は上が正）に直す
    for (DirectX::XMFLOAT2 &p : positions)
    {
        p.x = 2.0f * p.x / screenWidth - 1.0f;
        p.y = 1.0f - 2.0f * p.y / screenHeight;
    }

    D3D11_MAPPED_SUBRESOURCE mappedSubresource;
    HRESULT                  hr = dc->Map(vertexBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedSubresource);
    _ASSERT_EXPR(SUCCEEDED(hr), HRTrace(hr));

    Vertex *v = static_cast<Vertex *>(mappedSubresource.pData);
    for (int i = 0; i < 4; ++i)
    {
        v[i].position.x = positions[i].x;
        v[i].position.y = positions[i].y;
        v[i].position.z = dz;

        v[i].color = cornerColors[i];

        v[i].texcoord.x = texcoords[i].x / textureWidth;
        v[i].texcoord.y = texcoords[i].y / textureHeight;
    }

    dc->Unmap(vertexBuffer.Get(), 0);

    UINT stride = sizeof(Vertex);
    UINT offset = 0;
    dc->IASetVertexBuffers(0, 1, vertexBuffer.GetAddressOf(), &stride, &offset);
    dc->IASetInputLayout(inputLayout.Get());
    dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    dc->VSSetShader(vertexShader.Get(), nullptr, 0);
    dc->PSSetShader(pixelShader.Get(), nullptr, 0);
    dc->PSSetShaderResources(0, 1, shaderResourceView.GetAddressOf());
    dc->PSSetSamplers(0, 1, samplerState.GetAddressOf());

    dc->Draw(4, 0);
}



void Sprite::Render(ID3D11DeviceContext *dc,
                    float dx, float dy,
                    float dz,
                    float dw, float dh,
                    float sx, float sy,
                    float sw, float sh,
                    float angle,
                    float r, float g, float b, float a) const
{
    const DirectX::XMFLOAT4 c          = {r, g, b, a};
    const DirectX::XMFLOAT4 corners[4] = {c, c, c, c};
    Render(dc, dx, dy, dz, dw, dh, sx, sy, sw, sh, angle, corners);
}



void Sprite::Render(ID3D11DeviceContext *dc,
                    float dx, float dy,
                    float dz,
                    float dw, float dh,
                    float angle,
                    float r, float g, float b, float a) const
{
    Render(dc, dx, dy, dz, dw, dh, 0, 0, textureWidth, textureHeight, angle, r, g, b, a);
}