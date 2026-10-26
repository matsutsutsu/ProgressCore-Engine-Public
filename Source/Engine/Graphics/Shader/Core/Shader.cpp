#include "Shader.h"
#include "Engine/Graphics/Resource/ResourceManager.h"
#include "Engine/Graphics/Shader/Pass/ShadowMap.h"
#include "Engine/Graphics/Shader/ShaderResources.h"
#include "Engine/Core/Math/StringHash.h"
#include <algorithm>
#include <iterator> // std::size

// ---------------------------------------------------------------------------
// リフレクション
// ---------------------------------------------------------------------------

bool Shader::InitializeReflection(ID3D11Device *device, const std::string &csoPath)
{
    if (!m_reflection.LoadFromCompiledFile(csoPath.c_str())) return false;

    const auto &cbs = m_reflection.GetConstantBuffers();
    for (const auto &cbInfo : cbs)
    {
        // b8（SLOT_CB_SCENE）以降は Graphics などがバインドするフレーム共通のものなので作らない
        if (cbInfo.bindPoint >= SLOT_CB_SCENE)
        {
            continue;
        }
        m_constantBuffers[cbInfo.name].Create(device, cbInfo);
    }
    return true;
}

int Shader::FetchTextureSlot(const std::string &name)
{
    int slot = m_reflection.FindTextureSlot(name);
    if (slot != -1)
    {
        m_textureSlots[name] = slot;
        m_usedTextureSlots.push_back(slot);
    }
    return slot;
}

int Shader::FetchSamplerSlot(const std::string &name)
{
    int slot = m_reflection.FindSamplerSlot(name);
    if (slot != -1)
    {
        m_samplerSlots[name] = slot;
    }
    return slot;
}

// ---------------------------------------------------------------------------
// 描画時のバインド
// ---------------------------------------------------------------------------

void Shader::ApplyShaderPipeline(ID3D11DeviceContext *dc, ID3D11VertexShader *vs, ID3D11PixelShader *ps, ID3D11InputLayout *layout)
{
    dc->IASetInputLayout(layout);
    dc->VSSetShader(vs, nullptr, 0);
    dc->PSSetShader(ps, nullptr, 0);

    for (auto &pair : m_constantBuffers)
    {
        pair.second.BindVS(dc);
        pair.second.BindPS(dc);
    }
}

void Shader::UnbindResources(ID3D11DeviceContext *dc)
{
    ID3D11ShaderResourceView *nullSRV = nullptr;
    ID3D11Buffer             *nullCB  = nullptr;

    for (auto &pair : m_constantBuffers)
    {
        UINT slot = (UINT)pair.second.GetBindPoint();
        dc->VSSetConstantBuffers(slot, 1, &nullCB);
        dc->PSSetConstantBuffers(slot, 1, &nullCB);
    }

    for (int slot : m_usedTextureSlots)
    {
        dc->PSSetShaderResources((UINT)slot, 1, &nullSRV);
    }

    dc->VSSetShader(nullptr, nullptr, 0);
    dc->PSSetShader(nullptr, nullptr, 0);
    dc->IASetInputLayout(nullptr);
}

void Shader::UpdateMaterialProperties(ID3D11DeviceContext *dc, const MaterialData &mat,
                                      const MaterialData *fallbackMat)
{
    // テクスチャ:
    //   先にバインドし、テクスチャがあったかを覚えておく。「use + テクスチャ名」の変数
    //   （useAlbedoMap など）があれば、下の定数バッファにその有無を 1 / 0 で書く。
    //   フラグをマテリアルに持たせると、テクスチャだけ差し替えたときにずれて黒くなるので、ここで決める
    struct TextureFlag
    {
        uint32_t hash;
        float    value;
    };
    TextureFlag textureFlags[SLOT_SRV_SHADOW]; // マテリアルのテクスチャは t0〜t9 だけ
    size_t      textureFlagCount = 0;

    const auto &shaderTextures = m_reflection.GetTextures();

    for (const auto &texInfo : shaderTextures)
    {
        // t10（SLOT_SRV_SHADOW）以降はフレーム共通・ドローごとのもので、マテリアルのテクスチャではない
        if (texInfo.bindPoint >= SLOT_SRV_SHADOW) continue;
        // t4〜t6（深度・シーンの色・法線）はパス側がバインドする。マテリアルに無いからと
        // null をバインドすると、パスが入れたものを消してしまう
        if (texInfo.bindPoint == SLOT_SRV_SCENE_DEPTH ||
            texInfo.bindPoint == SLOT_SRV_SCENE_COLOR ||
            texInfo.bindPoint == SLOT_SRV_SCENE_NORMAL)
        {
            continue;
        }

        UINT          slot = texInfo.bindPoint;
        TextureHandle texHandle;

        // 名前のハッシュはリフレクションを読んだときに求めてある
        bool hasTex = mat.GetTexture(texInfo.nameHash, texHandle) && texHandle.IsValid();

        if (!hasTex && fallbackMat)
        {
            hasTex = fallbackMat->GetTexture(texInfo.nameHash, texHandle) && texHandle.IsValid();
        }

        if (hasTex)
        {
            ID3D11ShaderResourceView *srv = ResourceManager::Instance().GetTexture(texHandle);
            dc->PSSetShaderResources(slot, 1, &srv);

            if (std::find(m_usedTextureSlots.begin(), m_usedTextureSlots.end(), slot) == m_usedTextureSlots.end())
            {
                m_usedTextureSlots.push_back(slot);
            }
        }
        else
        {
            ID3D11ShaderResourceView *nullSRV = nullptr;
            dc->PSSetShaderResources(slot, 1, &nullSRV);
        }

        if (textureFlagCount < std::size(textureFlags))
        {
            textureFlags[textureFlagCount++] = {texInfo.useFlagHash, hasTex ? 1.0f : 0.0f};
        }
    }

    // 定数バッファ:
    //   定数バッファはシェーダー1つにつき1組で、そのシェーダーを使う全マテリアルが共有する。
    //   なのでマテリアルごとに必ず 0 に戻し、このマテリアルの値を書き直して毎回送る。
    //   「値を1つも書かなかったバッファは送らない」という最適化を入れてはいけない。
    //   前に描いたオブジェクトの値が残って次のオブジェクトに漏れる（関係ない敵やドローンが真っ黒になった）。
    //
    //   対象は b0 だけでなく、このシェーダーの b0〜b7 の全部。Basic は baseColor を
    //   b1 の CbMesh に置いているので、b0 だけにすると色が送られない。
    //
    //   逆に b0〜b7 にはマテリアル以外の値を置いてはいけない。ここで毎回 0 にするので、
    //   前に書いた値は消える。メッシュごと・オブジェクトごとの値は b8 以降に置く。
    for (auto &cbPair : m_constantBuffers)
    {
        auto &cb = cbPair.second;

        cb.ResetToZero();

        for (const auto &prop : mat.scalars)
        {
            cb.SetValue(prop.nameHash, prop.value);
        }
        for (const auto &prop : mat.colors)
        {
            cb.SetValue(prop.nameHash, prop.value);
        }
        // テクスチャの有無はマテリアルの値より後に書き、古いマテリアルファイルに残ったフラグを上書きする
        for (size_t i = 0; i < textureFlagCount; ++i)
        {
            cb.SetValue(textureFlags[i].hash, textureFlags[i].value);
        }
        // くり抜きのしきい値。Mask のマテリアルだけ alphaCutoff を送り、それ以外は 0（何も捨てない）にする。
        // glTF の alphaCutoff は不透明のマテリアルにも既定の 0.5 が入っているので、そのまま送ると穴があく
        cb.SetValue("alphaCutoff"_hash, mat.alphaMode == AlphaMode::Mask ? mat.alphaCutoff : 0.0f);

        cb.UpdateBuffer(dc);
    }
}

// ---------------------------------------------------------------------------
// ホットリロード
// ---------------------------------------------------------------------------

std::filesystem::file_time_type Shader::GetLastWriteTime(const std::wstring &filepath)
{
    if (std::filesystem::exists(filepath))
    {
        return std::filesystem::last_write_time(filepath);
    }
    return (std::filesystem::file_time_type::min)();
}

void Shader::ResetReflection()
{
    m_constantBuffers.clear();
    m_textureSlots.clear();
    m_samplerSlots.clear();
    m_usedTextureSlots.clear();
    m_reflection = ShaderReflect::ReflectionData();
}
