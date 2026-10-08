#pragma once
// ===========================================================================
// 概要: マテリアルの値（シェーダー・アルファの扱い・スカラー・色・テクスチャ）を持つデータ。
//
// 責務:
//   - 名前のハッシュをキーに、スカラー・色・テクスチャを設定・取得する
//   - .mat.json との相互変換（nlohmann::json）を定義する
//
// 備考: 値は名前のハッシュと値の組を vector に並べて持つ。プロパティは数個〜十数個なので、
//       map を使うより線形探索の方が速く、メモリも連続する。
//       シェーダーはリフレクションで得た変数名のハッシュで値を引く。
// ===========================================================================
#include <DirectXMath.h>
#include <vector>
#include <string>
#include "Engine/Core/Math/StringHash.h"
#include "Engine/Graphics/Resource/GraphicsTypes.h"
#include "Engine/Graphics/Resource/ResourcePool.h"
#include "ResourceTypes.h"

#include <json.hpp>

// ===================================================================================
// DirectXMath ⇔ json の変換（ADL で見つかるよう DirectX 名前空間に置く）
// ===================================================================================
namespace DirectX
{
    inline void to_json(nlohmann::json &j, const XMFLOAT4 &v)
    {
        j = nlohmann::json{{"x", v.x}, {"y", v.y}, {"z", v.z}, {"w", v.w}};
    }
    inline void from_json(const nlohmann::json &j, XMFLOAT4 &v)
    {
        j.at("x").get_to(v.x);
        j.at("y").get_to(v.y);
        j.at("z").get_to(v.z);
        j.at("w").get_to(v.w);
    }
} // namespace DirectX

struct MaterialScalar
{
    uint32_t nameHash; // シェーダー変数名のハッシュ
    float    value;
};

struct MaterialColor
{
    uint32_t          nameHash;
    DirectX::XMFLOAT4 value;
};

struct MaterialTextureRef
{
    uint32_t      nameHash;
    TextureHandle handle;    // 読み込んだテクスチャ（実行時だけ有効。保存しない）
    std::string   assetPath; // テクスチャのパス（保存する）
};

struct MaterialData
{
    uint32_t shaderHash = 0; // 使うシェーダーの名前のハッシュ

    AlphaMode alphaMode   = AlphaMode::Opaque;
    float     alphaCutoff = 0.5f; // AlphaMode::Mask で、これより小さいアルファを捨てる

    std::vector<MaterialScalar>     scalars;
    std::vector<MaterialColor>      colors;
    std::vector<MaterialTextureRef> textures;

    // ============================================================
    // 設定。同じハッシュがあれば上書きし、無ければ末尾に足す（線形探索）
    // ============================================================
    void SetScalar(uint32_t hash, float value)
    {
        for (auto &prop : scalars)
        {
            if (prop.nameHash == hash)
            {
                prop.value = value;
                return;
            }
        }
        scalars.push_back({hash, value});
    }

    void SetColor(uint32_t hash, const DirectX::XMFLOAT4 &value)
    {
        for (auto &prop : colors)
        {
            if (prop.nameHash == hash)
            {
                prop.value = value;
                return;
            }
        }
        colors.push_back({hash, value});
    }

    // path が空でなければ、保存用のパスも更新する
    void SetTexture(uint32_t hash, TextureHandle handle, const std::string &path = "")
    {
        for (auto &prop : textures)
        {
            if (prop.nameHash == hash)
            {
                prop.handle = handle;
                if (!path.empty()) prop.assetPath = path;
                return;
            }
        }
        textures.push_back({hash, handle, path});
    }

    // ============================================================
    // 取得。見つかれば out に入れて true（線形探索）
    // ============================================================
    bool GetScalar(uint32_t hash, float &outValue) const
    {
        for (const auto &prop : scalars)
        {
            if (prop.nameHash == hash)
            {
                outValue = prop.value;
                return true;
            }
        }
        return false;
    }

    bool GetColor(uint32_t hash, DirectX::XMFLOAT4 &outValue) const
    {
        for (const auto &prop : colors)
        {
            if (prop.nameHash == hash)
            {
                outValue = prop.value;
                return true;
            }
        }
        return false;
    }

    bool GetTexture(uint32_t hash, TextureHandle &outHandle) const
    {
        for (const auto &prop : textures)
        {
            if (prop.nameHash == hash)
            {
                outHandle = prop.handle;
                return true;
            }
        }
        return false;
    }
};

// ===================================================================================
// nlohmann::json との変換（.mat.json の形式はここで決まる）
// ===================================================================================

inline void to_json(nlohmann::json &j, const MaterialScalar &s)
{
    j = nlohmann::json{{"nameHash", s.nameHash}, {"value", s.value}};
}
inline void from_json(const nlohmann::json &j, MaterialScalar &s)
{
    j.at("nameHash").get_to(s.nameHash);
    j.at("value").get_to(s.value);
}

inline void to_json(nlohmann::json &j, const MaterialColor &c)
{
    j = nlohmann::json{{"nameHash", c.nameHash}, {"value", c.value}};
}
inline void from_json(const nlohmann::json &j, MaterialColor &c)
{
    j.at("nameHash").get_to(c.nameHash);
    j.at("value").get_to(c.value);
}

inline void to_json(nlohmann::json &j, const MaterialTextureRef &t)
{
    // handle は実行時だけのものなので保存せず、ハッシュとパスだけを保存する
    j = nlohmann::json{{"nameHash", t.nameHash}, {"assetPath", t.assetPath}};
}
inline void from_json(const nlohmann::json &j, MaterialTextureRef &t)
{
    j.at("nameHash").get_to(t.nameHash);
    j.at("assetPath").get_to(t.assetPath);
    t.handle = TextureHandle{}; // テクスチャは ResourceManager::LoadMaterialAsset が読み込む
}

inline void to_json(nlohmann::json &j, const MaterialData &m)
{
    j = nlohmann::json{
        {"shaderHash", m.shaderHash},
        {"alphaMode", static_cast<int>(m.alphaMode)}, // enum は int で保存する
        {"alphaCutoff", m.alphaCutoff},
        {"scalars", m.scalars},
        {"colors", m.colors},
        {"textures", m.textures}};
}
inline void from_json(const nlohmann::json &j, MaterialData &m)
{
    m.shaderHash = j.value("shaderHash", 0u);

    int mode = 0;
    if (j.contains("alphaMode"))
        j.at("alphaMode").get_to(mode);
    m.alphaMode = static_cast<AlphaMode>(mode);

    m.alphaCutoff = j.value("alphaCutoff", 0.5f);

    if (j.contains("scalars"))
        j.at("scalars").get_to(m.scalars);
    if (j.contains("colors"))
        j.at("colors").get_to(m.colors);
    if (j.contains("textures"))
        j.at("textures").get_to(m.textures);
}