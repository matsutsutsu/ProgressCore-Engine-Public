#pragma once
#include <DirectXMath.h>
#include <vector>
#include <string>
#include "Engine/Core/Math/StringHash.h" // あなたのStringHashヘッダ
#include "Engine/Graphics/Resource/GraphicsTypes.h"
#include "Engine/Graphics/Resource/ResourcePool.h"

// シリアライズ用のインクルード
#include <cereal/cereal.hpp>
#include <cereal/types/vector.hpp>
#include <cereal/types/string.hpp>

// DirectXMath のシリアライズヘルパー
namespace DirectX {
    template<class Archive>
    void serialize(Archive& archive, XMFLOAT4& v) {
        archive(cereal::make_nvp("x", v.x), cereal::make_nvp("y", v.y), cereal::make_nvp("z", v.z), cereal::make_nvp("w", v.w));
    }
}

// TextureHandle が何者であるかを、ここで直接定義する
using TextureResource = Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>;
using TextureHandle = CCL::Handle<TextureResource>;

// 1つのプロパティを表現する最小単位の構造体
struct MaterialScalar {
    uint32_t nameHash;
    float value;
    template<class Archive> void serialize(Archive& archive) { archive(CEREAL_NVP(nameHash), CEREAL_NVP(value)); }
};

struct MaterialColor {
    uint32_t nameHash;
    DirectX::XMFLOAT4 value;
    template<class Archive> void serialize(Archive& archive) { archive(CEREAL_NVP(nameHash), CEREAL_NVP(value)); }
};

struct MaterialTextureRef {
    uint32_t nameHash;
    TextureHandle handle;  // 実行時のみ有効
    std::string assetPath; // ★保存用（ファイルパス）

    template<class Archive> void save(Archive& archive) const {
        archive(CEREAL_NVP(nameHash), CEREAL_NVP(assetPath));
    }
    template<class Archive> void load(Archive& archive) {
        archive(CEREAL_NVP(nameHash), CEREAL_NVP(assetPath));
    }
};

// 完全に連続したメモリレイアウトを持つ新生 MaterialData
struct MaterialData {
    uint32_t shaderHash = 0; // 使用するシェーダー

    // =======================================================
    // アルファ・ブレンディングの設定 (コンパイルエラー解消)
    // =======================================================
    AlphaMode alphaMode = static_cast<AlphaMode>(0); // 通常は 0 (Opaque) が入ります
    float     alphaCutoff = 0.5f;

    // ヒープの断片化を防ぐため、std::vectorで一列に並べる
    std::vector<MaterialScalar>     scalars;
    std::vector<MaterialColor>      colors;
    std::vector<MaterialTextureRef> textures;

    // =======================================================
    // 値のセット（設定）関数: O(N) 線形探索 + 末尾追加
    // =======================================================
    void SetScalar(uint32_t hash, float value) {
        for (auto& prop : scalars) {
            if (prop.nameHash == hash) { prop.value = value; return; } // 上書き
        }
        scalars.push_back({ hash, value }); // 新規追加
    }

    void SetColor(uint32_t hash, const DirectX::XMFLOAT4& value) {
        for (auto& prop : colors) {
            if (prop.nameHash == hash) { prop.value = value; return; }
        }
        colors.push_back({ hash, value });
    }

    // テクスチャのパスも受け取って保存できるようにする
    void SetTexture(uint32_t hash, TextureHandle handle, const std::string& path = "") {
        for (auto& prop : textures) {
            if (prop.nameHash == hash) {
                prop.handle = handle;
                if (!path.empty()) prop.assetPath = path;
                return;
            }
        }
        textures.push_back({ hash, handle, path });
    }

    // =======================================================
    // 値のゲット（取得）関数: O(N) 線形探索
    // =======================================================
    bool GetScalar(uint32_t hash, float& outValue) const {
        for (const auto& prop : scalars) {
            if (prop.nameHash == hash) { outValue = prop.value; return true; }
        }
        return false;
    }

    bool GetColor(uint32_t hash, DirectX::XMFLOAT4& outValue) const {
        for (const auto& prop : colors) {
            if (prop.nameHash == hash) { outValue = prop.value; return true; }
        }
        return false;
    }

    bool GetTexture(uint32_t hash, TextureHandle& outHandle) const {
        for (const auto& prop : textures) {
            if (prop.nameHash == hash) { outHandle = prop.handle; return true; }
        }
        return false;
    }

    // ★追加: 構造体全体のシリアライズ
    template<class Archive>
    void serialize(Archive& archive) {
        archive(CEREAL_NVP(shaderHash), CEREAL_NVP(alphaMode), CEREAL_NVP(alphaCutoff), CEREAL_NVP(scalars), CEREAL_NVP(colors), CEREAL_NVP(textures));
    }
};