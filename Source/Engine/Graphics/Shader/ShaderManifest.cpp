/// @file  ShaderManifest.cpp
/// @brief ShaderManifest の実装。shaders.json を FileSystem 経由で読み、
///        マテリアル/カスタムシェーダーを ShaderRegistry へ登録する。
///
///  Why: シェーダー追加のたびに Graphics.cpp を編集・再ビルドしていた摩擦を消す。
///       マテリアルは JSON だけで完結。ErrorShader(マゼンタ)へ落ちる前に
///       cso 欠落を警告し、typo を早期検知できるようにしている。
#include "ShaderManifest.h"

#include "Engine/Graphics/Shader/ShaderRegistry.h"
#include "Engine/Platform/Core/FileSystem.h"
#include "Engine/Platform/Core/Logger.h"
#include "Engine/Core/Math/StringHash.h"   // CCL::Utils::HashString（"_hash" リテラルと同一のハッシュ）

// カスタムシェーダー具象クラス（RegisterShader<T> に「型」が必要なため実体を include）
#include "Engine/Graphics/Shader/Material/OutlineShader.h"
#include "Engine/Graphics/Shader/Material/ShadowMapShader.h"
#include "Engine/Graphics/Shader/Material/ParticleShader.h"

#include <json.hpp>
#include <functional>
#include <unordered_map>

namespace {

// ---------------------------------------------------------------------------
// ASCII 専用のワイド変換。シェーダーのパスは全て ASCII なのでこれで十分。
// （HLSL パスに日本語を入れる運用にするなら、ここを UTF-8→UTF-16 変換に差し替える）
// ---------------------------------------------------------------------------
std::wstring ToWide(const std::string& s)
{
    return std::wstring(s.begin(), s.end());
}

// ---------------------------------------------------------------------------
// カスタムシェーダーのレジストラ表。
// JSON の "class" 文字列から C++ 型 T を選び RegisterShader<T> を呼ぶ。
//
// ★ 新しいカスタム C++ シェーダーを追加するとき「だけ」ここに1行足す。
//    （マテリアルシェーダーは JSON だけで完結し、このファイルは触らない）
//    文字列から型を作る手段が言語に無いため、この対応表は原理的に不可避。
// ---------------------------------------------------------------------------
using CustomRegistrar = std::function<void(ShaderRegistry&, uint32_t)>;

const std::unordered_map<std::string, CustomRegistrar>& CustomRegistrars()
{
    static const std::unordered_map<std::string, CustomRegistrar> table = {
        { "OutlineShader",   [](ShaderRegistry& r, uint32_t h) { r.RegisterShader<OutlineShader>(h);   } },
        { "ShadowMapShader", [](ShaderRegistry& r, uint32_t h) { r.RegisterShader<ShadowMapShader>(h); } },
        { "ParticleShader",  [](ShaderRegistry& r, uint32_t h) { r.RegisterShader<ParticleShader>(h);  } },
    };
    return table;
}

// 1つのマテリアルエントリを登録する。成功で true。
bool RegisterMaterialEntry(const nlohmann::json& e, ShaderRegistry& reg)
{
    if (!e.contains("name") || !e["name"].is_string()) {
        CCL_LOG_ERROR(LogCategory::Render,
            "[ShaderManifest] materialShaders エントリに name がありません。スキップします。");
        return false;
    }

    const std::string name = e["name"].get<std::string>();

    // 命名規約（全シェーダー統一）: <name>VS / <name>PS
    const std::string vsCso  = "Assets/Shader/" + name + "VS.cso";
    const std::string psCso  = "Assets/Shader/" + name + "PS.cso";
    const std::string vsHlsl = "Shader/Material/" + name + "/" + name + "VS.hlsl";
    const std::string psHlsl = "Shader/Material/" + name + "/" + name + "PS.hlsl";

    // 早期警告: 登録は遅延生成なのでここでは cso を読まないが、
    //           実在チェックだけ行い、実行時にマゼンタで気づく前に typo を検知する。
    if (!FileSystem::Exists(vsCso))
        CCL_LOG_WARN(LogCategory::Render, "[ShaderManifest] VS cso が見つかりません: %s", vsCso.c_str());
    if (!FileSystem::Exists(psCso))
        CCL_LOG_WARN(LogCategory::Render, "[ShaderManifest] PS cso が見つかりません: %s", psCso.c_str());

    const uint32_t hash = CCL::Utils::HashString(name.c_str());
    reg.RegisterMaterialShader(hash, vsCso, ToWide(vsHlsl), psCso, ToWide(psHlsl));

    CCL_LOG_INFO(LogCategory::Render, "[ShaderManifest] material 登録: %s (hash=%u)", name.c_str(), hash);
    return true;
}

// 1つのカスタムエントリを登録する。成功で true。
bool RegisterCustomEntry(const nlohmann::json& e, ShaderRegistry& reg)
{
    if (!e.contains("name")  || !e["name"].is_string() ||
        !e.contains("class") || !e["class"].is_string()) {
        CCL_LOG_ERROR(LogCategory::Render,
            "[ShaderManifest] customShaders エントリに name/class がありません。スキップします。");
        return false;
    }

    const std::string name = e["name"].get<std::string>();
    const std::string cls  = e["class"].get<std::string>();

    auto it = CustomRegistrars().find(cls);
    if (it == CustomRegistrars().end()) {
        CCL_LOG_ERROR(LogCategory::Render,
            "[ShaderManifest] 未知のカスタムクラス '%s' (name=%s)。"
            "ShaderManifest.cpp の CustomRegistrars に登録行を追加してください。",
            cls.c_str(), name.c_str());
        return false;
    }

    const uint32_t hash = CCL::Utils::HashString(name.c_str());
    it->second(reg, hash);

    CCL_LOG_INFO(LogCategory::Render, "[ShaderManifest] custom 登録: %s <%s> (hash=%u)",
        name.c_str(), cls.c_str(), hash);
    return true;
}

} // namespace

bool ShaderManifest::LoadAndRegister(const std::string& manifestVirtualPath,
                                    ShaderRegistry& registry)
{
    // 1. VFS 経由で読み込む（std::ifstream は使わない。将来の .pak/暗号化に透過対応するため）
    const std::string text = FileSystem::ReadAllText(manifestVirtualPath);
    if (text.empty()) {
        CCL_LOG_ERROR(LogCategory::Render,
            "[ShaderManifest] マニフェストが読み込めません（空/欠落）: %s", manifestVirtualPath.c_str());
        return false;
    }

    // 2. パース
    nlohmann::json root;
    try {
        root = nlohmann::json::parse(text);
    }
    catch (const std::exception& ex) {
        CCL_LOG_ERROR(LogCategory::Render,
            "[ShaderManifest] JSON パース失敗: %s (%s)", manifestVirtualPath.c_str(), ex.what());
        return false;
    }

    int registered = 0;

    // 3. マテリアルシェーダー
    if (root.contains("materialShaders") && root["materialShaders"].is_array()) {
        for (const auto& e : root["materialShaders"]) {
            if (RegisterMaterialEntry(e, registry)) ++registered;
        }
    }

    // 4. カスタムシェーダー
    if (root.contains("customShaders") && root["customShaders"].is_array()) {
        for (const auto& e : root["customShaders"]) {
            if (RegisterCustomEntry(e, registry)) ++registered;
        }
    }

    if (registered == 0) {
        CCL_LOG_ERROR(LogCategory::Render,
            "[ShaderManifest] 1件も登録できませんでした: %s", manifestVirtualPath.c_str());
        return false;
    }

    CCL_LOG_SUCCESS(LogCategory::Render,
        "[ShaderManifest] %d 個のシェーダーを登録しました。", registered);
    return true;
}