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
// 備考: 登録は Graphics::Initialize がまとめて行う。
// ===========================================================================
#include <unordered_map>
#include <functional>
#include <memory>
#include "Engine/Graphics/Shader/Core/Shader.h"
#include "Engine/Graphics/Shader/Material/MaterialShader.h"
#include "Engine/Core/Math/StringHash.h"
#include <unordered_set>
#include "Engine/Platform/Core/Logger.h"
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

    // MaterialShader を名前で登録する。ハッシュは名前から作り、エディタのシェーダー選択にも登録した順に出る。
    // ファイルのパスは名前（N）から決まる
    //   cso : Assets/Shader/<N>VS.cso, Assets/Shader/<N>PS.cso
    //   hlsl: Shader/Material/<N>/<N>VS.hlsl, Shader/Material/<N>/<N>PS.hlsl（ホットリロードで監視する）
    void RegisterMaterialShader(const std::string &name)
    {
        const std::string  vsCso = "Assets/Shader/" + name + "VS.cso";
        const std::string  psCso = "Assets/Shader/" + name + "PS.cso";
        const std::wstring wName(name.begin(), name.end()); // 名前は ASCII だけなので、1文字ずつ広げれば足りる
        const std::wstring vsHlsl = L"Shader/Material/" + wName + L"/" + wName + L"VS.hlsl";
        const std::wstring psHlsl = L"Shader/Material/" + wName + L"/" + wName + L"PS.hlsl";

        const uint32_t shaderHash = CCL::Utils::HashString(name);
        m_factories[shaderHash]   = [vsCso, vsHlsl, psCso, psHlsl](ID3D11Device *device) {
            return std::make_unique<MaterialShader>(device, vsCso, vsHlsl, psCso, psHlsl);
        };
        m_shaderSortIDs[shaderHash] = m_nextSortID++;
        RegisterName(shaderHash, name);
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

    // 登録した順の (表示名, ハッシュ)。エディタのシェーダー選択が使う。
    // 選んだ名前からハッシュを計算し直さなくて済むよう、ハッシュも持つ
    const std::vector<std::pair<std::string, uint32_t>> &GetRegisteredNames() const
    {
        return m_orderedNames;
    }

private:
    void RegisterName(uint32_t shaderHash, const std::string &name)
    {
        if (m_hashToName.find(shaderHash) == m_hashToName.end())
        {
            m_orderedNames.push_back({name, shaderHash});
        }
        m_hashToName[shaderHash] = name; // 2回目（デバイスを作り直したとき）からは名前を書き換えるだけで、順番は変えない
    }

    std::unordered_map<uint32_t, std::function<std::unique_ptr<Shader>(ID3D11Device *)>> m_factories;
    std::unordered_map<uint32_t, std::unique_ptr<Shader>>                                m_shaders;

    uint8_t                               m_nextSortID = 0; // 次に振る番号
    std::unordered_map<uint32_t, uint8_t> m_shaderSortIDs;

    std::unordered_set<uint32_t> m_warnedMissing; // 未登録の警告を出したハッシュ

    std::unordered_map<uint32_t, std::string>     m_hashToName;   // 2回目の登録かを見分ける
    std::vector<std::pair<std::string, uint32_t>> m_orderedNames; // 登録した順
};