#pragma once
#include <map>
#include <string>
#include <DirectXMath.h>
#include <wrl.h>
#include <d3d11.h> // ID3D11ShaderResourceView用

#include "Engine/Graphics/Shader/Material/Core/ShaderId.h"
#include "Engine/Graphics/Resource/GraphicsTypes.h"
#include "Engine/Graphics/Resource/ResourcePool.h"

// TextureHandle が何者であるかを、ここで直接定義する
using TextureResource = Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>;
using TextureHandle   = CCL::Handle<TextureResource>;

// 純粋なデータ構造体。ECSやModelのことは知らなくていい。
struct MaterialData
{
    std::string name = "Material"; // マテリアル名

    ShaderId shaderId = ShaderId::Phong;
    AlphaMode alphaMode = AlphaMode::Opaque;
    float alphaCutoff = 0.5f;

    // ※std::mapの使用についての厳しい指摘は後述します
    std::map<std::string, DirectX::XMFLOAT4> colors;
    std::map<std::string, float> scalars;
    // ComPtrではなく、TextureHandleを受け取るようにする
    std::map<std::string, TextureHandle> textures;
};