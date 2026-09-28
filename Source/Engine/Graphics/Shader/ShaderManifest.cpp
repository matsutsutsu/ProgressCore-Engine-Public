#include "ShaderManifest.h"

#include "Engine/Graphics/Shader/ShaderRegistry.h"
#include "Engine/Graphics/RHI/PipelineState.h"
#include "Engine/Platform/Core/FileSystem.h"
#include "Engine/Platform/Core/Logger.h"
#include "Engine/Core/Math/StringHash.h" // CCL::Utils::HashString（"_hash" と同じハッシュ）

// RegisterShader<T> に型が要るので、専用クラスのヘッダーを include する
#include "Engine/Graphics/Shader/Material/OutlineShader.h"
#include "Engine/Graphics/Shader/Material/ShadowMapShader.h"
#include "Engine/Graphics/Shader/Material/ParticleShader.h"
#include "Engine/Graphics/Shader/Material/NormalPrepassShader.h"

#include <json.hpp>
#include <functional>
#include <unordered_map>

namespace
{

    // ASCII だけのワイド文字列への変換（シェーダーのパスは ASCII だけなので足りる）。
    // パスに日本語を使うなら、UTF-8 → UTF-16 の変換に替える
    std::wstring ToWide(const std::string &s)
    {
        return std::wstring(s.begin(), s.end());
    }

    // customShaders の "class" → RegisterShader<T> の表。
    // 専用の C++ クラスのシェーダーを足すときだけ、ここに1行足す
    using CustomRegistrar = std::function<void(ShaderRegistry &, uint32_t)>;

    const std::unordered_map<std::string, CustomRegistrar> &CustomRegistrars()
    {
        static const std::unordered_map<std::string, CustomRegistrar> table = {
            {"OutlineShader", [](ShaderRegistry &r, uint32_t h) { r.RegisterShader<OutlineShader>(h); }},
            {"ShadowMapShader", [](ShaderRegistry &r, uint32_t h) { r.RegisterShader<ShadowMapShader>(h); }},
            {"ParticleShader", [](ShaderRegistry &r, uint32_t h) { r.RegisterShader<ParticleShader>(h); }},
            {"NormalPrepassShader", [](ShaderRegistry &r, uint32_t h) { r.RegisterShader<NormalPrepassShader>(h); }},
        };
        return table;
    }

    // JSON の文字列 → RenderState.h の enum。知らない値なら false（呼び出し側で警告を出す）
    bool ParseBlend(const std::string &s, BlendState &out)
    {
        if (s == "opaque")
        {
            out = BlendState::Opaque;
            return true;
        }
        if (s == "transparency" || s == "alpha")
        {
            out = BlendState::Transparency;
            return true;
        }
        if (s == "additive" || s == "add")
        {
            out = BlendState::Additive;
            return true;
        }
        if (s == "subtract" || s == "subtraction")
        {
            out = BlendState::Subtraction;
            return true;
        }
        if (s == "multiply")
        {
            out = BlendState::Multiply;
            return true;
        }
        return false;
    }

    bool ParseCull(const std::string &s, RasterizerState &out)
    {
        if (s == "back")
        {
            out = RasterizerState::SolidCullBack;
            return true;
        }
        if (s == "none")
        {
            out = RasterizerState::SolidCullNone;
            return true;
        }
        return false;
    }

    bool ParseDepth(const std::string &s, DepthState &out)
    {
        if (s == "testWrite" || s == "default")
        {
            out = DepthState::TestAndWrite;
            return true;
        }
        if (s == "testOnly" || s == "readOnly")
        {
            out = DepthState::TestOnly;
            return true;
        }
        if (s == "writeOnly")
        {
            out = DepthState::WriteOnly;
            return true;
        }
        if (s == "none" || s == "noTestNoWrite")
        {
            out = DepthState::NoTestNoWrite;
            return true;
        }
        // testOnly と同じ（どちらも LESS_EQUAL で書かない）。この名前で書いた JSON も読めるように残す
        if (s == "testLessEqual")
        {
            out = DepthState::TestOnly;
            return true;
        }
        return false;
    }

    // blend / cull / depth のどれかがあれば PSO の設定を out に作って true。何も無ければ false。
    // blend が半透明の系統（Transparency / Additive / Subtraction / Multiply）なら DefaultTransparent
    // （深度を書かない・両面）、それ以外なら DefaultOpaque（深度を書く・裏面カリング）を土台にし、
    // cull / depth があればその項目だけ変える。{"blend": "additive"} だけで加算に合う設定になる
    bool BuildPsoDesc(const nlohmann::json &e, const std::string &name, PipelineStateDesc &out)
    {
        const bool hasBlend = e.contains("blend") && e["blend"].is_string();
        const bool hasCull  = e.contains("cull") && e["cull"].is_string();
        const bool hasDepth = e.contains("depth") && e["depth"].is_string();
        if (!hasBlend && !hasCull && !hasDepth) return false;

        BlendState blend = BlendState::Opaque;
        if (hasBlend && !ParseBlend(e["blend"].get<std::string>(), blend))
            CCL_LOG_WARN(LogCategory::Render, "[ShaderManifest] %s: 未知の blend 値 '%s'。opaque 扱い。",
                         name.c_str(), e["blend"].get<std::string>().c_str());

        const bool transparentish =
            (blend == BlendState::Transparency || blend == BlendState::Additive ||
             blend == BlendState::Subtraction || blend == BlendState::Multiply);

        out       = transparentish ? PipelineStateDesc::DefaultTransparent()
                                   : PipelineStateDesc::DefaultOpaque();
        out.blend = blend;

        if (hasCull)
        {
            RasterizerState r;
            if (ParseCull(e["cull"].get<std::string>(), r))
                out.rasterizer = r;
            else
                CCL_LOG_WARN(LogCategory::Render, "[ShaderManifest] %s: 未知の cull 値 '%s'。",
                             name.c_str(), e["cull"].get<std::string>().c_str());
        }
        if (hasDepth)
        {
            DepthState d;
            if (ParseDepth(e["depth"].get<std::string>(), d))
                out.depth = d;
            else
                CCL_LOG_WARN(LogCategory::Render, "[ShaderManifest] %s: 未知の depth 値 '%s'。",
                             name.c_str(), e["depth"].get<std::string>().c_str());
        }
        return true;
    }

    bool RegisterMaterialEntry(const nlohmann::json &e, ShaderRegistry &reg)
    {
        if (!e.contains("name") || !e["name"].is_string())
        {
            CCL_LOG_ERROR(LogCategory::Render,
                          "[ShaderManifest] materialShaders エントリに name がありません。スキップします。");
            return false;
        }

        const std::string name = e["name"].get<std::string>();

        const std::string vsCso  = "Assets/Shader/" + name + "VS.cso";
        const std::string psCso  = "Assets/Shader/" + name + "PS.cso";
        const std::string vsHlsl = "Shader/Material/" + name + "/" + name + "VS.hlsl";
        const std::string psHlsl = "Shader/Material/" + name + "/" + name + "PS.hlsl";

        // cso を読むのは初めて使うときだが、名前の打ち間違いに起動時に気付けるよう、ここで有無だけ見る
        if (!FileSystem::Exists(vsCso))
            CCL_LOG_WARN(LogCategory::Render, "[ShaderManifest] VS cso が見つかりません: %s", vsCso.c_str());
        if (!FileSystem::Exists(psCso))
            CCL_LOG_WARN(LogCategory::Render, "[ShaderManifest] PS cso が見つかりません: %s", psCso.c_str());

        const uint32_t hash = CCL::Utils::HashString(name.c_str());

        PipelineStateDesc psoDesc;
        const bool        hasPso = BuildPsoDesc(e, name, psoDesc);
        reg.RegisterMaterialShader(hash, vsCso, ToWide(vsHlsl), psCso, ToWide(psHlsl),
                                   hasPso ? &psoDesc : nullptr);
        reg.RegisterName(hash, name);

        CCL_LOG_INFO(LogCategory::Render, "[ShaderManifest] material 登録: %s (hash=%u%s)",
                     name.c_str(), hash, hasPso ? ", 独自PSO" : "");
        return true;
    }

    bool RegisterCustomEntry(const nlohmann::json &e, ShaderRegistry &reg)
    {
        if (!e.contains("name") || !e["name"].is_string() ||
            !e.contains("class") || !e["class"].is_string())
        {
            CCL_LOG_ERROR(LogCategory::Render,
                          "[ShaderManifest] customShaders エントリに name/class がありません。スキップします。");
            return false;
        }

        const std::string name = e["name"].get<std::string>();
        const std::string cls  = e["class"].get<std::string>();

        auto it = CustomRegistrars().find(cls);
        if (it == CustomRegistrars().end())
        {
            CCL_LOG_ERROR(LogCategory::Render,
                          "[ShaderManifest] 未知のカスタムクラス '%s' (name=%s)。"
                          "ShaderManifest.cpp の CustomRegistrars に登録行を追加してください。",
                          cls.c_str(), name.c_str());
            return false;
        }

        const uint32_t hash = CCL::Utils::HashString(name.c_str());
        it->second(reg, hash);
        reg.RegisterName(hash, name);

        CCL_LOG_INFO(LogCategory::Render, "[ShaderManifest] custom 登録: %s <%s> (hash=%u)",
                     name.c_str(), cls.c_str(), hash);
        return true;
    }

} // namespace

bool ShaderManifest::LoadAndRegister(const std::string &manifestVirtualPath,
                                     ShaderRegistry    &registry)
{
    // std::ifstream ではなく FileSystem で読む（アセットを .pak にまとめたときもこのまま読めるように）
    const std::string text = FileSystem::ReadAllText(manifestVirtualPath);
    if (text.empty())
    {
        CCL_LOG_ERROR(LogCategory::Render,
                      "[ShaderManifest] マニフェストが読み込めません（空/欠落）: %s", manifestVirtualPath.c_str());
        return false;
    }

    nlohmann::json root;
    try
    {
        root = nlohmann::json::parse(text);
    }
    catch (const std::exception &ex)
    {
        CCL_LOG_ERROR(LogCategory::Render,
                      "[ShaderManifest] JSON パース失敗: %s (%s)", manifestVirtualPath.c_str(), ex.what());
        return false;
    }

    int registered = 0;

    if (root.contains("materialShaders") && root["materialShaders"].is_array())
    {
        for (const auto &e : root["materialShaders"])
        {
            if (RegisterMaterialEntry(e, registry)) ++registered;
        }
    }

    if (root.contains("customShaders") && root["customShaders"].is_array())
    {
        for (const auto &e : root["customShaders"])
        {
            if (RegisterCustomEntry(e, registry)) ++registered;
        }
    }

    if (registered == 0)
    {
        CCL_LOG_ERROR(LogCategory::Render,
                      "[ShaderManifest] 1件も登録できませんでした: %s", manifestVirtualPath.c_str());
        return false;
    }

    CCL_LOG_SUCCESS(LogCategory::Render,
                    "[ShaderManifest] %d 個のシェーダーを登録しました。", registered);
    return true;
}