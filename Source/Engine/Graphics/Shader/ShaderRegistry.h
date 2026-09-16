#pragma once
#include <unordered_map>
#include <functional>
#include <memory>
#include "Engine/Graphics/Shader/Core/Shader.h"
#include "Engine/Graphics/Shader/Material/MaterialShader.h"
#include "Engine/Core/Math/StringHash.h"
#include <unordered_set>
#include "Engine/Platform/Core/Logger.h"
#include "Engine/Graphics/RHI/PipelineState.h"
#include <optional>
#include <string>

class ShaderRegistry
{
public:
    static ShaderRegistry &Instance()
    {
        static ShaderRegistry instance;
        return instance;
    }

    // シングルトンの寿命が尽きる前に、明示的にリソースを手放す
    void ClearAll()
    {
        m_shaders.clear();
        m_factories.clear();
        m_shaderSortIDs.clear();
        m_warnedMissing.clear();
    }

    // エンジン起動時等にシェーダーを登録する関数
    template <typename T>
    void RegisterShader(uint32_t shaderHash)
    {
        m_factories[shaderHash] = [](ID3D11Device *device) {
            return std::make_unique<T>(device);
        };
        // 新しいシェーダーが登録されるたびに、0から順にソート用の連番IDを割り当てる
        m_shaderSortIDs[shaderHash] = m_nextSortID++;
    }

    // =======================================================
    // 汎用マテリアルシェーダー用の登録関数
    // =======================================================
    void RegisterMaterialShader(uint32_t           shaderHash,
                                const std::string &vsCso, const std::wstring &vsHlsl,
                                const std::string &psCso, const std::wstring &psHlsl,
                                const PipelineStateDesc *psoDesc = nullptr)
    {
        // PSO 設計図は factory ラムダの寿命まで生存させる必要があるため optional にコピー保持
        std::optional<PipelineStateDesc> psoCopy;
        if (psoDesc) psoCopy = *psoDesc;
        m_factories[shaderHash] = [vsCso, vsHlsl, psCso, psHlsl, psoCopy](ID3D11Device *device) {
            return std::make_unique<MaterialShader>(device, vsCso, vsHlsl, psCso, psHlsl,
                                                    psoCopy ? &*psoCopy : nullptr);
        };
        m_shaderSortIDs[shaderHash] = m_nextSortID++;
    }

    // 全シェーダーの監視を一斉実行する関数
    void UpdateHotReload(ID3D11Device *device)
    {
        for (auto &pair : m_shaders)
        {
            if (pair.second) pair.second->CheckAndReload(device);
        }
    }

    // 必要な時に生成、あるいはキャッシュを返す。未登録なら ErrorShader を返す。
    Shader *GetShader(uint32_t shaderHash, ID3D11Device *device)
    {
        // 1. すでにインスタンスがあればそれを返す
        auto it = m_shaders.find(shaderHash);
        if (it != m_shaders.end()) return it->second.get();

        // 2. ファクトリから生成
        auto factoryIt = m_factories.find(shaderHash);
        if (factoryIt != m_factories.end())
        {
            m_shaders[shaderHash] = factoryIt->second(device);
            return m_shaders[shaderHash].get();
        }

        // 3. フォールバック：未登録ハッシュはマゼンタの ErrorShader で代替する
        //    Error 自身が未登録の場合のみ nullptr
        if (shaderHash != "Error"_hash)
        {
            // 毎フレーム呼ばれ得るので、同一ハッシュは一度だけ警告（スパム防止）
            if (m_warnedMissing.insert(shaderHash).second)
            {
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
    uint8_t GetSortID(uint32_t shaderHash) const
    {
        auto it = m_shaderSortIDs.find(shaderHash);
        return it != m_shaderSortIDs.end() ? it->second : 0;
    }

    // ============================================================
    //  表示名レジストリ（エディタのシェーダー選択コンボ用）
    // ------------------------------------------------------------
    //  ShaderManifest が shaders.json から読んだ "name" をここに登録する。
    //  material/custom を問わず、JSON に書かれた登録順を保持する。
    //  これにより、新しいシェーダーを shaders.json に足すだけで
    //  エディタのドロップダウンにも自動で反映される
    //  （MaterialComponentMeta.cpp の手書き配列を廃止するための土台）。
    // ============================================================
    void RegisterName(uint32_t shaderHash, const std::string &name)
    {
        if (m_hashToName.find(shaderHash) == m_hashToName.end())
        {
            m_orderedNames.push_back({name, shaderHash});
        }
        m_hashToName[shaderHash] = name; // 再登録（ホットリロード等）時は上書きのみ
    }

    /**
     * @brief 登録順の (表示名, ハッシュ) 一覧。
     * @details エディタの ImGui::Combo 等はこれを毎回変換して使う想定
     *          （名前→ハッシュの再計算をしなくて済むよう、ハッシュも一緒に返す）。
     */
    const std::vector<std::pair<std::string, uint32_t>> &GetRegisteredNames() const
    {
        return m_orderedNames;
    }

private:
    std::unordered_map<uint32_t, std::function<std::unique_ptr<Shader>(ID3D11Device *)>> m_factories;
    std::unordered_map<uint32_t, std::unique_ptr<Shader>>                                m_shaders;

    // ソートID管理用
    uint8_t                               m_nextSortID = 0;
    std::unordered_map<uint32_t, uint8_t> m_shaderSortIDs;

    std::unordered_set<uint32_t> m_warnedMissing; // 警告済みハッシュ（once-warn）

    std::unordered_map<uint32_t, std::string>     m_hashToName;   // 重複登録の検出用
    std::vector<std::pair<std::string, uint32_t>> m_orderedNames; // 表示順を保持
};