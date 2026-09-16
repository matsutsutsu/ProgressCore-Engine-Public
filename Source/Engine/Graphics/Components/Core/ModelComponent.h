#pragma once
#include <cstdint>
#include "Engine/Graphics/Resource/Model.h"
#include "Engine/Graphics/Resource/ResourceManager.h"
#include "Engine/Core/Math/StringHash.h"

struct ModelComponent
{
    // =================================================================
    // [Data Layout]
    //   modelHandle(8) + assetHash(4) + isRuntime(1) + ownsHandle(1) = 14B
    //   ※ ランタイム生成メッシュ（ダンジョンのチャンク等）対応のため
    //      isRuntime / ownsHandle を追加。ファイル由来モデルの挙動は不変。
    // =================================================================
    ModelInstanceHandle modelHandle;   // 8 byte (Index 4 + Gen 4)
    uint32_t            assetHash = 0; // 4 byte (文字列パスのハッシュ)

    // ▼ ランタイム生成ハンドル保護用フラグ
    //   isRuntime  : true ならファイル(assetHash)から再生成できない実体ハンドル
    //   ownsHandle : true のときだけデストラクタで UnloadModelInstance を行う
    bool isRuntime  = false;
    bool ownsHandle = true;

    ModelComponent() = default;

    // パスから初期化
    ModelComponent(const char *path)
    {
        SetModel(path);
    }

    // =================================================================
    // [Rule of 5] ECS Chunk 移動に伴うライフサイクル管理
    // =================================================================

    ~ModelComponent()
    {
        // 所有者のときだけ解放（ランタイムハンドルのコピーは非所有なので解放しない）
        if (modelHandle.IsValid() && ownsHandle)
        {
            ResourceManager::Instance().UnloadModelInstance(modelHandle);
            modelHandle = ModelInstanceHandle{};
        }
    }

    // コピーコンストラクタ
    //   - ファイル由来 (assetHash!=0)   : 従来どおりハッシュから新インスタンスを複製
    //   - ランタイム生成 (isRuntime)    : 再生成不可なのでハンドルを共有し「非所有」にする
    //                                     （二重解放防止。元が生きている前提）
    ModelComponent(const ModelComponent &other)
        : assetHash(other.assetHash), isRuntime(other.isRuntime)
    {
        if (isRuntime)
        {
            modelHandle = other.modelHandle;
            ownsHandle  = false; // コピーは Unload しない
        }
        else if (assetHash != 0)
        {
            modelHandle = ResourceManager::Instance().CreateModelInstanceFromHash(assetHash);
            ownsHandle  = true;
        }
    }

    // コピー代入
    ModelComponent &operator=(const ModelComponent &other)
    {
        if (this != &other)
        {
            if (modelHandle.IsValid() && ownsHandle)
            {
                ResourceManager::Instance().UnloadModelInstance(modelHandle);
            }
            assetHash = other.assetHash;
            isRuntime = other.isRuntime;
            if (isRuntime)
            {
                modelHandle = other.modelHandle;
                ownsHandle  = false;
            }
            else if (assetHash != 0)
            {
                modelHandle = ResourceManager::Instance().CreateModelInstanceFromHash(assetHash);
                ownsHandle  = true;
            }
            else
            {
                modelHandle = ModelInstanceHandle{};
                ownsHandle  = true;
            }
        }
        return *this;
    }

    // ムーブコンストラクタ（noexcept 必須：vector 再配置でコピーに落ちないように）
    //   所有権（ownsHandle 含む）をそのまま移譲し、元を空にする。
    ModelComponent(ModelComponent &&other) noexcept
        : modelHandle(other.modelHandle), assetHash(other.assetHash),
          isRuntime(other.isRuntime), ownsHandle(other.ownsHandle)
    {
        other.modelHandle = ModelInstanceHandle{};
        other.assetHash   = 0;
        other.ownsHandle  = false; // 元は所有権を失う
    }

    // ムーブ代入
    ModelComponent &operator=(ModelComponent &&other) noexcept
    {
        if (this != &other)
        {
            if (modelHandle.IsValid() && ownsHandle)
            {
                ResourceManager::Instance().UnloadModelInstance(modelHandle);
            }
            modelHandle = other.modelHandle;
            assetHash   = other.assetHash;
            isRuntime   = other.isRuntime;
            ownsHandle  = other.ownsHandle;

            other.modelHandle = ModelInstanceHandle{};
            other.assetHash   = 0;
            other.ownsHandle  = false;
        }
        return *this;
    }

    // =================================================================
    // [Accessors]
    // =================================================================

    Model *GetModel() const
    {
        return ResourceManager::Instance().GetModelInstance(modelHandle);
    }

    void SetModel(const char *path)
    {
        if (modelHandle.IsValid() && ownsHandle)
        {
            ResourceManager::Instance().UnloadModelInstance(modelHandle);
            modelHandle = ModelInstanceHandle{};
        }

        if (path && path[0] != '\0')
        {
            assetHash   = CCL::Utils::HashString(path);
            modelHandle = ResourceManager::Instance().CreateModelInstance(path);
            isRuntime   = false;
            ownsHandle  = true;
        }
        else
        {
            assetHash  = 0;
            isRuntime  = false;
            ownsHandle = true;
        }
    }

    // ランタイム生成 Model（Model::CreateFromData → RegisterModelInstance）の
    //    ハンドルを差し込む専用セッター。ダンジョンのチャンク等で使用。
    void SetRuntimeModel(ModelInstanceHandle handle)
    {
        if (modelHandle.IsValid() && ownsHandle)
        {
            ResourceManager::Instance().UnloadModelInstance(modelHandle);
        }
        modelHandle = handle;
        assetHash   = 0; // ファイル由来でない
        isRuntime   = true;
        ownsHandle  = true; // 最初の所有者
    }
};