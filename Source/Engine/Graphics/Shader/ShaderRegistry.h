#pragma once
#include <unordered_map>
#include <functional>
#include <memory>
#include "Engine/Graphics/Shader/Core/Shader.h"
#include "Engine/Graphics/Shader/Material/MaterialShader.h"
#include "Engine/Core/Math/StringHash.h"
#include <unordered_set>
#include "Engine/Platform/Core/Logger.h"


class ShaderRegistry {
public:
    static ShaderRegistry& Instance() { static ShaderRegistry instance; return instance; }

    // シングルトンの寿命が尽きる前に、明示的にリソースを手放す
    void ClearAll() {
        _shaders.clear();
        _factories.clear();
        _shaderSortIDs.clear();
        _warnedMissing.clear();
    }

    // エンジン起動時等にシェーダーを登録する関数
    template<typename T>
    void RegisterShader(uint32_t shaderHash) {
        _factories[shaderHash] = [](ID3D11Device* device) {
            return std::make_unique<T>(device);
            };
        // 新しいシェーダーが登録されるたびに、0から順にソート用の連番IDを割り当てる
        _shaderSortIDs[shaderHash] = _nextSortID++;
    }

    // =======================================================
    // 汎用マテリアルシェーダー用の登録関数
    // =======================================================
    void RegisterMaterialShader(uint32_t shaderHash,
        const std::string& vsCso, const std::wstring& vsHlsl,
        const std::string& psCso, const std::wstring& psHlsl) {
        _factories[shaderHash] = [vsCso, vsHlsl, psCso, psHlsl](ID3D11Device* device) {
            return std::make_unique<MaterialShader>(device, vsCso, vsHlsl, psCso, psHlsl);
            };
        _shaderSortIDs[shaderHash] = _nextSortID++;
    }

    // 全シェーダーの監視を一斉実行する関数
    void UpdateHotReload(ID3D11Device* device) {
        for (auto& pair : _shaders) {
            if (pair.second) pair.second->CheckAndReload(device);
        }
    }

    // 必要な時に生成、あるいはキャッシュを返す。未登録なら ErrorShader を返す。
    Shader* GetShader(uint32_t shaderHash, ID3D11Device* device) {
        // 1. すでにインスタンスがあればそれを返す
        auto it = _shaders.find(shaderHash);
        if (it != _shaders.end()) return it->second.get();

        // 2. ファクトリから生成
        auto factoryIt = _factories.find(shaderHash);
        if (factoryIt != _factories.end()) {
            _shaders[shaderHash] = factoryIt->second(device);
            return _shaders[shaderHash].get();
        }

        // 3. フォールバック：未登録ハッシュはマゼンタの ErrorShader で代替する
        //    Error 自身が未登録の場合のみ nullptr
        if (shaderHash != "Error"_hash) {
            // 毎フレーム呼ばれ得るので、同一ハッシュは一度だけ警告（スパム防止）
            if (_warnedMissing.insert(shaderHash).second) {
                CCL_LOG_WARN(LogCategory::Core,
                    "ShaderRegistry: 未登録シェーダー要求 (hash=0x%08X)。ErrorShader(マゼンタ)で代替。",
                    shaderHash);
            }
            return GetShader("Error"_hash, device);
        }

        CCL_LOG_ERROR(LogCategory::Core,
            "ShaderRegistry: ErrorShader 自体が未登録です。Graphics.cpp で登録してください。");
        return nullptr;
    }

    // ハッシュ値から、8bitに収まる小さな連番IDを取得する
    uint8_t GetSortID(uint32_t shaderHash) const {
        auto it = _shaderSortIDs.find(shaderHash);
        return it != _shaderSortIDs.end() ? it->second : 0;
    }

private:
    std::unordered_map<uint32_t, std::function<std::unique_ptr<Shader>(ID3D11Device*)>> _factories;
    std::unordered_map<uint32_t, std::unique_ptr<Shader>> _shaders;

    // ソートID管理用
    uint8_t _nextSortID = 0;
    std::unordered_map<uint32_t, uint8_t> _shaderSortIDs;

    std::unordered_set<uint32_t> _warnedMissing; // 警告済みハッシュ（once-warn）

};