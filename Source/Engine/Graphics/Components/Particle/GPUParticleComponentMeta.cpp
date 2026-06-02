// =====================================================================
// GPUParticleComponentMeta.cpp
//   ★ 設計刷新: パーティクルを「普通のシリアライズ可能コンポーネント」にする。
//
//   変更点:
//     1. CbParticle ⇔ json 変換を 1 箇所 (ParticleConfigCodec) に集約
//     2. Serialize/Deserialize<GPUParticleComponent> を実装 (CustomSerialize)
//        → シーン保存・Prefab・プリセットが全部この 1 つを共有する
//     3. プリセット保存/読込を FileSystem 経由に変更
//     4. Fields() は空のまま (CustomSerialize なので Fields は使わない)
//
//   これにより:
//     - ComponentGuiRegistry.cpp の重複 ConfigToJson/JsonToConfig は削除可能
//     - Prefab.cpp の手書きパースは ComponentMetaJson::Deserialize に置換可能
// =====================================================================
#include "Engine/Graphics/Components/Particle/GPUParticleComponent.h"
#include "Engine/Graphics/RHI/Graphics.h"
#include "Engine/Platform/Core/Dialog.h"
#include "Engine/Platform/Core/FileSystem.h"
#include "Engine/Graphics/Shader/ShaderShared.h"

#include "Engine/Serialization/Registry/ComponentRegistry.h"
#include "Engine/Serialization/Registry/ComponentRegistry_Editor.h"
#include "Engine/Serialization/Meta/ComponentMeta.h"
#include "Engine/Serialization/Meta/ComponentMetaJson.h"

#include <json.hpp>
#include <cmath>
#include <filesystem>

#ifdef ENABLE_EDITOR
#include "Editor/Inspector/ComponentGuiRegistry.h"
#include "Engine/Serialization/Meta/ComponentMetaImGui.h"
#endif

using json = nlohmann::json;

// =====================================================================
// ★ 唯一の変換ロジック: CbParticle ⇔ json
//   ここだけを真実とする。他のどこにも CbParticle の JSON 化を書かない。
// =====================================================================
namespace {

    inline float SafeF(float v) { return std::isfinite(v) ? v : 0.0f; }

    /** @brief GPU パーティクル設定を json に変換する (config 部分のみ) */
    json CbParticleToJson(const GpuData::CbParticle& cfg)
    {
        json j;
        // スカラー
        j["type"] = cfg.type;
        j["colorMode"] = cfg.color_mode;
        j["emissionMode"] = cfg.emission_mode;
        j["renderMode"] = cfg.render_mode;
        j["animMode"] = cfg.sprite_anim_mode;
        j["burstCount"] = cfg.burst_count;
        j["gravity"] = SafeF(cfg.gravity);
        j["noiseScale"] = SafeF(cfg.noise_scale);
        j["noiseStrength"] = SafeF(cfg.noise_strength);
        j["velocityStretch"] = SafeF(cfg.velocity_stretch);
        j["stretchX"] = SafeF(cfg.emission_stretch_x);
        j["stretchZ"] = SafeF(cfg.emission_stretch_z);
        j["heightVolumeY"] = SafeF(cfg.particle_offset_y);
        // ベクトル (min/max 等のペア)
        j["spawnDelay"] = { SafeF(cfg.spawn_delay.x),         SafeF(cfg.spawn_delay.y) };
        j["lifespan"] = { SafeF(cfg.lifespan.x),            SafeF(cfg.lifespan.y) };
        j["speed"] = { SafeF(cfg.emission_speed.x),      SafeF(cfg.emission_speed.y) };
        j["angularSpeed"] = { SafeF(cfg.emission_angular_speed.x), SafeF(cfg.emission_angular_speed.y) };
        j["radius"] = { SafeF(cfg.emission_offset.x),     SafeF(cfg.emission_offset.y) };
        j["coneAngle"] = { SafeF(cfg.emission_cone_angle.x), SafeF(cfg.emission_cone_angle.y) };
        j["size"] = { SafeF(cfg.emission_size.x),       SafeF(cfg.emission_size.y) };
        j["fade"] = { SafeF(cfg.fade_duration.x),       SafeF(cfg.fade_duration.y) };
        j["uvScroll"] = { SafeF(cfg.uv_scroll_speed.x),     SafeF(cfg.uv_scroll_speed.y) };
        j["color"] = { SafeF(cfg.manual_color.x), SafeF(cfg.manual_color.y),
                                 SafeF(cfg.manual_color.z), SafeF(cfg.manual_color.w) };
        j["spriteGrid"] = { cfg.sprite_sheet_grid.x, cfg.sprite_sheet_grid.y };
        return j;
    }

    /** @brief json から GPU パーティクル設定を読み込む (config 部分のみ) */
    void JsonToCbParticle(const json& j, GpuData::CbParticle& cfg)
    {
        auto rd = [&](const char* k, auto& dst) { if (j.contains(k)) dst = j[k]; };
        auto rd2 = [&](const char* k, auto& v) {
            if (j.contains(k) && j[k].is_array() && j[k].size() >= 2) {
                v.x = j[k][0]; v.y = j[k][1];
            }
            };

        rd("type", cfg.type);
        rd("colorMode", cfg.color_mode);
        rd("emissionMode", cfg.emission_mode);
        rd("renderMode", cfg.render_mode);
        rd("animMode", cfg.sprite_anim_mode);
        rd("burstCount", cfg.burst_count);
        rd("gravity", cfg.gravity);
        rd("noiseScale", cfg.noise_scale);
        rd("noiseStrength", cfg.noise_strength);
        rd("velocityStretch", cfg.velocity_stretch);
        rd("stretchX", cfg.emission_stretch_x);
        rd("stretchZ", cfg.emission_stretch_z);
        rd("heightVolumeY", cfg.particle_offset_y);

        rd2("spawnDelay", cfg.spawn_delay);
        rd2("lifespan", cfg.lifespan);
        rd2("speed", cfg.emission_speed);
        rd2("angularSpeed", cfg.emission_angular_speed);
        rd2("radius", cfg.emission_offset);
        rd2("coneAngle", cfg.emission_cone_angle);
        rd2("size", cfg.emission_size);
        rd2("fade", cfg.fade_duration);
        rd2("uvScroll", cfg.uv_scroll_speed);

        if (j.contains("color") && j["color"].is_array() && j["color"].size() >= 4) {
            auto c = j["color"];
            cfg.manual_color = { c[0], c[1], c[2], c[3] };
        }
        if (j.contains("spriteGrid") && j["spriteGrid"].is_array() && j["spriteGrid"].size() >= 2) {
            cfg.sprite_sheet_grid.x = j["spriteGrid"][0];
            cfg.sprite_sheet_grid.y = j["spriteGrid"][1];
        }
    }

    std::string ToRelativePath(const std::filesystem::path& absPath)
    {
        std::error_code ec;
        auto rel = std::filesystem::relative(absPath, std::filesystem::current_path(), ec);
        return (!ec && !rel.empty()) ? rel.generic_string() : absPath.generic_string();
    }

} // namespace

// =====================================================================
// ★ CustomSerialize: GPUParticleComponent の Serialize/Deserialize
//   これが「唯一のシリアライズ経路」。シーン・Prefab・プリセット全部で使われる。
// =====================================================================
namespace ComponentMetaJson {

    template <>
    void Serialize<GPUParticleComponent>(json& j, const GPUParticleComponent& comp, SaveContext*)
    {
        j["maxParticles"] = comp.maxParticles;
        j["texturePath"] = std::string(comp.texturePath);
        j["colorRampPath"] = std::string(comp.colorRampPath);
        j["config"] = CbParticleToJson(comp.config);
    }

    template <>
    void Deserialize<GPUParticleComponent>(const json& j, GPUParticleComponent& comp, LoadContext*)
    {
        if (j.contains("maxParticles")) {
            comp.maxParticles = j["maxParticles"];
            comp.config.max_particle_count = comp.maxParticles;
        }
        if (j.contains("texturePath")) {
            std::string p = j["texturePath"];
            strcpy_s(comp.texturePath, sizeof(comp.texturePath), p.c_str());
        }
        if (j.contains("colorRampPath")) {
            std::string p = j["colorRampPath"];
            strcpy_s(comp.colorRampPath, sizeof(comp.colorRampPath), p.c_str());
        }
        if (j.contains("config")) {
            JsonToCbParticle(j["config"], comp.config);
        }
        // ロード後は GPU リソース再構築を要求する
        comp.isInitialized = false;
        comp.needsGpuInit = true;
    }

} // namespace ComponentMetaJson

// =====================================================================
// HasCustomSerialize を true にして、上の特化を使わせる
// =====================================================================
namespace ComponentMetaJson {
    template <> struct HasCustomSerialize<GPUParticleComponent> : std::true_type {};
}

// =====================================================================
// プリセット保存/読込ヘルパー (FileSystem 経由・上の Serialize を再利用)
// =====================================================================
namespace {
    /** @brief プリセットを保存。シーン保存と同じ Serialize を使うのでスキーマが一致する */
    bool SavePreset(const std::string& path, const GPUParticleComponent& comp)
    {
        json j;
        ComponentMetaJson::Serialize<GPUParticleComponent>(j, comp, nullptr);
        return FileSystem::WriteAllText(path, j.dump(4));
    }

    /** @brief プリセットを読込。Prefab/シーンと同じ Deserialize を使う */
    bool LoadPreset(const std::string& path, GPUParticleComponent& comp)
    {
        if (!FileSystem::Exists(path)) return false;
        const std::string text = FileSystem::ReadAllText(path);
        if (text.empty()) return false;
        try {
            json j = json::parse(text);
            ComponentMetaJson::Deserialize<GPUParticleComponent>(j, comp, nullptr);
            return true;
        }
        catch (...) { return false; }
    }
}

// =====================================================================
// ComponentMeta<GPUParticleComponent>
// =====================================================================
template <> struct ComponentMeta<GPUParticleComponent> {
    static constexpr bool        registered = true;
    static constexpr const char* displayName = "GPUParticle";
    static constexpr bool        hasCustomGui = true;

#ifdef ENABLE_EDITOR
    static bool CustomGui(GPUParticleComponent& compRef,
        unsigned long long = 0, void* = nullptr)
    {
        bool changed = false;
        GPUParticleComponent* p = &compRef;
        auto& cfg = p->config;
        HWND hWnd = Graphics::Instance().GetWindowHandle();

        // ---- プリセット保存/読込 (FileSystem 経由に統一) ----
        if (ImGui::Button(u8"プリセット保存 (Save Preset)")) {
            if (auto path = Engine::FileDialog::SavePreset(
                "GPUParticlePreset", u8"Save Particle Preset", {}, hWnd))
            {
                SavePreset(path->string(), *p);   // ★ 統一された Serialize を使う
            }
        }
        ImGui::SameLine();
        if (ImGui::Button(u8"プリセット読込 (Load Preset)")) {
            if (auto path = Engine::FileDialog::OpenPreset(
                "GPUParticlePreset", u8"Load Particle Preset", hWnd))
            {
                if (LoadPreset(path->string(), *p)) changed = true;  // ★ 統一された Deserialize
            }
        }
        ImGui::Separator();

        // ---- 以下、各パラメータの ImGui 編集 (既存コードのまま) ----
        // (発生・形状・挙動・見た目・テクスチャ ... 省略せず既存のものを貼り付け)
        // ※ ここは表示と編集だけなので変更不要。
        //    変更が必要なのは「保存/読込が SavePreset/LoadPreset を呼ぶ」点だけ。

        // 例: 基本設定
        if (ImGui::DragInt(u8"最大数 (Max Particles)", &p->maxParticles, 16.0f, 1, 1000000)) {
            cfg.max_particle_count = p->maxParticles;
            p->isInitialized = false;
            changed = true;
        }
        // ... (残りの DragFloat 等は既存のものをそのまま) ...

        return changed;
    }
#else
    static bool CustomGui(GPUParticleComponent&, unsigned long long, void*) { return false; }
#endif

    // CustomSerialize を使うので Fields は空でよい
    static const std::vector<FieldDescriptor>& Fields()
    {
        static const std::vector<FieldDescriptor> empty;
        return empty;
    }
};

// =====================================================================
// 登録
// =====================================================================
REGISTER_COMPONENT(GPUParticleComponent, "GPUParticle")
REGISTER_COMPONENT_EDITOR_GUI(GPUParticleComponent)