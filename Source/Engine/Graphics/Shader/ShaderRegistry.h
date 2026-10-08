#pragma once
// ===========================================================================
// 概要: シェーダーをハッシュで登録し、初めて使うときに作ってキャッシュする入れ物。
//
// 責務:
//   - シェーダーの作り方（ファクトリ）をハッシュごとに登録する
//   - GetShader で作る・キャッシュを返す。未登録なら ErrorShader を返す
//   - RenderQueue の並べ替えで使う 8bit の番号を、登録した順に振る
//   - エディタのシェーダー選択に出す名前を、登録した順に持つ
//
// 備考: 登録は ShaderManifest が shaders.json から行う。
//       ErrorShader だけは、shaders.json が壊れていても使えるよう Graphics::Initialize が C++ から登録する。
// ===========================================================================
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

    // 全部を捨てる（静的変数の破棄より前に、デバイスと一緒に GPU リソースを手放すため）
    void ClearAll()
    {
        m_shaders.clear();
        m_factories.clear();
        m_shaderSortIDs.clear();
        m_warnedMissing.clear();
    }

    // T(device) で作るシェーダーを登録する
    template <typename T>
    void RegisterShader(uint32_t shaderHash)
    {
        m_factories[shaderHash] = [](ID3D11Device *device) {
            return std::make_unique<T>(device);
        };
        m_shaderSortIDs[shaderHash] = m_nextSortID++;
    }

    // MaterialShader を登録する。psoDesc は写して持つ（nullptr なら PSO を持たない）
    void RegisterMaterialShader(uint32_t           shaderHash,
                                const std::string &vsCso, const std::wstring &vsHlsl,
                                const std::string &psCso, const std::wstring &psHlsl,
                                const PipelineStateDesc *psoDesc = nullptr)
    {
        // ファクトリは後で呼ばれるので、psoDesc の指す先ではなく値をラムダに持たせる
        std::optional<PipelineStateDesc> psoCopy;
        if (psoDesc) psoCopy = *psoDesc;
        m_factories[shaderHash] = [vsCso, vsHlsl, psCso, psHlsl, psoCopy](ID3D11Device *device) {
            return std::make_unique<MaterialShader>(device, vsCso, vsHlsl, psCso, psHlsl,
                                                    psoCopy ? &*psoCopy : nullptr);
        };
        m_shaderSortIDs[shaderHash] = m_nextSortID++;
    }

    // 作ったシェーダーの CheckAndReload を呼ぶ（Graphics が毎フレーム呼ぶ）
    void UpdateHotReload(ID3D11Device *device)
    {
        for (auto &pair : m_shaders)
        {
            if (pair.second) pair.second->CheckAndReload(device);
        }
    }

    // 作ってあればそれを、無ければファクトリで作って返す。未登録なら ErrorShader を返す
    Shader *GetShader(uint32_t shaderHash, ID3D11Device *device)
    {
        auto it = m_shaders.find(shaderHash);
        if (it != m_shaders.end()) return it->second.get();

        auto factoryIt = m_factories.find(shaderHash);
        if (factoryIt != m_factories.end())
        {
            m_shaders[shaderHash] = factoryIt->second(device);
            return m_shaders[shaderHash].get();
        }

        // 未登録なら、画面で気付けるようマゼンタの ErrorShader を返す。
        // ErrorShader も未登録なら nullptr
        if (shaderHash != "Error"_hash)
        {
            // 毎フレーム呼ばれるので、同じハッシュの警告は1回だけ出す
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

    // 登録した順の番号（0〜255）。未登録なら 0（最初に登録する ErrorShader と同じ番号。未登録は ErrorShader で描くので合っている）
    uint8_t GetSortID(uint32_t shaderHash) const
    {
        auto it = m_shaderSortIDs.find(shaderHash);
        return it != m_shaderSortIDs.end() ? it->second : 0;
    }

    // ============================================================
    // 表示名（エディタのシェーダー選択用）
    // ============================================================
    //   ShaderManifest が shaders.json の "name" を、書かれた順に登録する。
    //   エディタは自分で一覧を持たずにこれを使うので、shaders.json に足すだけで選択肢にも出る

    void RegisterName(uint32_t shaderHash, const std::string &name)
    {
        if (m_hashToName.find(shaderHash) == m_hashToName.end())
        {
            m_orderedNames.push_back({name, shaderHash});
        }
        m_hashToName[shaderHash] = name; // 2回目からは名前を書き換えるだけで、順番は変えない
    }

    // 登録した順の (表示名, ハッシュ)。選んだ名前からハッシュを計算し直さなくて済むよう、ハッシュも持つ
    const std::vector<std::pair<std::string, uint32_t>> &GetRegisteredNames() const
    {
        return m_orderedNames;
    }

private:
    std::unordered_map<uint32_t, std::function<std::unique_ptr<Shader>(ID3D11Device *)>> m_factories;
    std::unordered_map<uint32_t, std::unique_ptr<Shader>>                                m_shaders;

    uint8_t                               m_nextSortID = 0; // 次に振る番号
    std::unordered_map<uint32_t, uint8_t> m_shaderSortIDs;

    std::unordered_set<uint32_t> m_warnedMissing; // 未登録の警告を出したハッシュ

    std::unordered_map<uint32_t, std::string>     m_hashToName;   // 2回目の登録かを見分ける
    std::vector<std::pair<std::string, uint32_t>> m_orderedNames; // 登録した順
};