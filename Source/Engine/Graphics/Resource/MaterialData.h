#pragma once
#include <DirectXMath.h>
#include <vector>
#include <string>
#include "Engine/Core/Math/StringHash.h" // テクスチャ名をハッシュ化して引くため
#include "Engine/Graphics/Resource/GraphicsTypes.h"
#include "Engine/Graphics/Resource/ResourcePool.h"

// 新しく作った型定義専用ヘッダーを読み込む
#include "ResourceTypes.h"

#include <json.hpp>

// ===================================================================================
// DirectXMath ⇔ json 変換 (ADL解決用)
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

// 1つのプロパティを表現する最小単位の構造体
struct MaterialScalar
{
    uint32_t nameHash;
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
    TextureHandle handle;    // 実行時のみ有効
    std::string   assetPath; // 保存用（ファイルパス）
};

// 完全に連続したメモリレイアウトを持つ新生 MaterialData
struct MaterialData
{
    uint32_t shaderHash = 0; // 使用するシェーダー

    // =======================================================
    // アルファ・ブレンディングの設定 (コンパイルエラー解消)
    // =======================================================
    AlphaMode alphaMode   = static_cast<AlphaMode>(0); // 通常は 0 (Opaque) が入ります
    float     alphaCutoff = 0.5f;

    // ヒープの断片化を防ぐため、std::vectorで一列に並べる
    std::vector<MaterialScalar>     scalars;
    std::vector<MaterialColor>      colors;
    std::vector<MaterialTextureRef> textures;

    // =======================================================
    // 値のセット（設定）関数: O(N) 線形探索 + 末尾追加
    // =======================================================
    void SetScalar(uint32_t hash, float value)
    {
        for (auto &prop : scalars)
        {
            if (prop.nameHash == hash)
            {
                prop.value = value;
                return;
            } // 上書き
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

    // テクスチャのパスも受け取って保存できるようにする
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

    // =======================================================
    // 値のゲット（取得）関数: O(N) 線形探索
    // =======================================================
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
// nlohmann::json 変換関数 (Single Source of Truth)
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
    // 実行時専用の handle は保存せず、パスとハッシュのみ保存する
    j = nlohmann::json{{"nameHash", t.nameHash}, {"assetPath", t.assetPath}};
}
inline void from_json(const nlohmann::json &j, MaterialTextureRef &t)
{
    j.at("nameHash").get_to(t.nameHash);
    j.at("assetPath").get_to(t.assetPath);
    t.handle = TextureHandle{}; // ロード直後はハンドル未解決状態にする
}

inline void to_json(nlohmann::json &j, const MaterialData &m)
{
    j = nlohmann::json{
        {"shaderHash", m.shaderHash},
        {"alphaMode", static_cast<int>(m.alphaMode)}, // enumはintとして保存
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