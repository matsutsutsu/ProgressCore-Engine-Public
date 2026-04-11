#pragma once
#include <memory>
#include "Engine/Graphics/Resource/Model.h" // Modelクラスはソース内に存在します [5]
#include "Engine/Graphics/Resource/ResourceManager.h"
#include <string>


struct ModelComponent
{
    ModelInstanceHandle modelHandle;
    std::string assetPath;

	ModelComponent() = default;

    // パターンA: パスから生成（シリアライズやエディタで使用）
    ModelComponent(const std::string& path) {
        SetModel(path);
    }

    // ==============================================================
        // ★ ECSのChunk移動に耐えるための厳密なライフサイクル管理 (Rule of 5)
        // ==============================================================

    ~ModelComponent() {
        if (modelHandle.IsValid()) {
            ResourceManager::Instance().UnloadModelInstance(modelHandle);
            modelHandle = ModelInstanceHandle{};
        }
    }

    // コピー構築 (別エンティティとして同じモデルをロードする)
    ModelComponent(const ModelComponent& other) : assetPath(other.assetPath) {
        if (!assetPath.empty()) {
            modelHandle = ResourceManager::Instance().CreateModelInstance(assetPath.c_str());
        }
    }

    // コピー代入
    ModelComponent& operator=(const ModelComponent& other) {
        if (this != &other) {
            SetModel(other.assetPath);
        }
        return *this;
    }

    // ムーブ構築 (チャンク間の引っ越しで呼ばれる。所有権を移す)
    ModelComponent(ModelComponent&& other) noexcept
        : modelHandle(other.modelHandle), assetPath(std::move(other.assetPath)) {
        other.modelHandle = ModelInstanceHandle{}; // 元の住人の鍵を取り上げる
    }


    // ムーブ代入
    ModelComponent& operator=(ModelComponent&& other) noexcept {
        if (this != &other) {
            if (modelHandle.IsValid()) {
                ResourceManager::Instance().UnloadModelInstance(modelHandle);
            }
            modelHandle = other.modelHandle;
            assetPath = std::move(other.assetPath);
            other.modelHandle = ModelInstanceHandle{};
        }
        return *this;
    }

    // ==============================================================

    // System側から実体へアクセスするためのアクセッサ
    Model* GetModel() const {
        return ResourceManager::Instance().GetModelInstance(modelHandle);
    }

    void SetModel(const std::string& path) {
        // 既存のリソースがあれば解放
        if (modelHandle.IsValid()) {
            ResourceManager::Instance().UnloadModelInstance(modelHandle);
            modelHandle = ModelInstanceHandle{};
        }

        assetPath = path;

        if (!assetPath.empty()) {
            // プールに新しいインスタンスの生成を依頼
            modelHandle = ResourceManager::Instance().CreateModelInstance(assetPath.c_str());
        }
    }
};
