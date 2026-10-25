#pragma once
// ===========================================================================
// 概要: Entity に表示するモデルを1つ持たせるコンポーネント。
//
// 責務:
//   - モデルインスタンスのハンドルと、読み込み元パスのハッシュを持つ
//   - コピー・ムーブ・破棄に合わせてモデルインスタンスを生成・解放する
//
// 備考: ECS は Chunk 間の移動でコンポーネントをコピー・ムーブするので、
//       Rule of 5 を明示して二重解放と解放漏れを防ぐ。
//       コピーは同じハンドルを共有せず、assetHash から新しいインスタンスを作る
//       （アニメーション姿勢などをインスタンスごとに持つため）。
// ===========================================================================
#include <cstdint>
#include "Engine/Graphics/Resource/Model.h"
#include "Engine/Graphics/Resource/ResourceManager.h"
#include "Engine/Core/Math/StringHash.h"

struct ModelComponent
{
    ModelInstanceHandle modelHandle;           // モデルインスタンスのハンドル（このコンポーネントが解放する）
    uint32_t            assetHash   = 0;       // 読み込み元パスのハッシュ。0 はモデル無し
    bool                generateLod = false;   // モデルを読み込むときに LOD も作るか（SetModel より前に決める）

    ModelComponent() = default;

    ModelComponent(const char *path)
    {
        SetModel(path);
    }

    // ============================================================
    // Rule of 5
    // ============================================================

    ~ModelComponent()
    {
        if (modelHandle.IsValid())
        {
            ResourceManager::Instance().UnloadModelInstance(modelHandle);
            modelHandle = ModelInstanceHandle{};
        }
    }

    // assetHash から新しいインスタンスを作る
    ModelComponent(const ModelComponent &other)
        : assetHash(other.assetHash), generateLod(other.generateLod)
    {
        if (assetHash != 0)
        {
            modelHandle = ResourceManager::Instance().CreateModelInstanceFromHash(assetHash);
        }
    }

    ModelComponent &operator=(const ModelComponent &other)
    {
        if (this != &other)
        {
            if (modelHandle.IsValid())
            {
                ResourceManager::Instance().UnloadModelInstance(modelHandle);
            }
            assetHash   = other.assetHash;
            generateLod = other.generateLod;
            if (assetHash != 0)
            {
                modelHandle = ResourceManager::Instance().CreateModelInstanceFromHash(assetHash);
            }
            else
            {
                modelHandle = ModelInstanceHandle{};
            }
        }
        return *this;
    }

    // noexcept が無いと vector の再配置でコピーが選ばれ、インスタンスが作り直される
    ModelComponent(ModelComponent &&other) noexcept
        : modelHandle(other.modelHandle), assetHash(other.assetHash), generateLod(other.generateLod)
    {
        other.modelHandle = ModelInstanceHandle{};
        other.assetHash   = 0;
    }

    ModelComponent &operator=(ModelComponent &&other) noexcept
    {
        if (this != &other)
        {
            if (modelHandle.IsValid())
            {
                ResourceManager::Instance().UnloadModelInstance(modelHandle);
            }
            modelHandle = other.modelHandle;
            assetHash   = other.assetHash;
            generateLod = other.generateLod;

            other.modelHandle = ModelInstanceHandle{};
            other.assetHash   = 0;
        }
        return *this;
    }

    // ============================================================
    // アクセサ
    // ============================================================

    Model *GetModel() const
    {
        return ResourceManager::Instance().GetModelInstance(modelHandle);
    }

    // 今のモデルを解放し、path のモデルに差し替える。空文字ならモデル無しにする
    void SetModel(const char *path)
    {
        if (modelHandle.IsValid())
        {
            ResourceManager::Instance().UnloadModelInstance(modelHandle);
            modelHandle = ModelInstanceHandle{};
        }

        if (path && path[0] != '\0')
        {
            // LOD の予約は、インスタンスを作る（＝共有データを読み込む）より前に済ませる
            if (generateLod)
            {
                ResourceManager::Instance().RequestModelLod(path);
            }

            assetHash   = CCL::Utils::HashString(path);
            modelHandle = ResourceManager::Instance().CreateModelInstance(path);
        }
        else
        {
            assetHash = 0;
        }
    }
};
