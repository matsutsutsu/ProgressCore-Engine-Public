// =====================================================================
// GPUParticleComponentMeta.cpp  (手書き / 自動生成しない)
//
//   パーティクルを「普通のシリアライズ可能コンポーネント」にする Meta。
//
//   設計:
//     1. desc <-> json 変換を 1 箇所 (DescCodec) に集約 = 唯一の真実
//     2. Serialize/Deserialize<GPUParticleComponent> = CustomSerialize
//        → シーン保存・Prefab・プリセットが全部この 1 つを共有
//     3. プリセット保存/読込は FileSystem 経由で同じ Serialize を再利用
//     4. Fields() は空 (CustomSerialize なので使わない)
//
//   現行構造への対応:
//     - 旧 comp.config(CbParticle) / comp.maxParticles を廃止
//     - すべて comp.desc (ParticleEmitterDesc) に集約
//     - GUI 編集後は MarkDescChanged() で staticCB 再アップロードを要求
//     - maxCount 変更時のみ isInitialized=false (バッファ再構築)
//     - テクスチャは選択時に即ロード (バッファ再構築は不要)
// =====================================================================
#include "Engine/Graphics/Components/Particle/GPUParticleComponent.h"
#include "Engine/Graphics/RHI/Graphics.h"
#include "Engine/Platform/Core/Dialog.h"
#include "Engine/Platform/Core/FileSystem.h"

#include "Engine/Serialization/Registry/ComponentRegistry.h"
#include "Engine/Serialization/Registry/ComponentRegistry_Editor.h"
#include "Engine/Serialization/Meta/ComponentMeta.h"
#include "Engine/Serialization/Meta/ComponentMetaJson.h"

#include <json.hpp>
#include <cmath>
#include <filesystem>
#include <string>

#ifdef ENABLE_EDITOR
#include "Editor/Inspector/ComponentGuiRegistry.h"
#include "Engine/Serialization/Meta/ComponentMetaImGui.h"
#endif

using json = nlohmann::json;

// =====================================================================
// ★ 唯一の変換ロジック: ParticleEmitterDesc <-> json
//   ここだけを真実とする。他のどこにも desc の JSON 化を書かない。
// =====================================================================
namespace {

    inline float SafeF(float v) { return std::isfinite(v) ? v : 0.0f; }

    // ---- desc -> json ----------------------------------------------
    void WriteDescToJson(json& j, const ParticleEmitterDesc& d)
    {
        // ベクトル系
        j["emitRadius"] = { SafeF(d.emitRadius.x),   SafeF(d.emitRadius.y) };
        j["emitSize"] = { SafeF(d.emitSize.x),     SafeF(d.emitSize.y) };
        j["coneAngle"] = { SafeF(d.coneAngle.x),    SafeF(d.coneAngle.y) };
        j["emitSpeed"] = { SafeF(d.emitSpeed.x),    SafeF(d.emitSpeed.y) };
        j["angularSpeed"] = { SafeF(d.angularSpeed.x), SafeF(d.angularSpeed.y) };
        j["lifespan"] = { SafeF(d.lifespan.x),     SafeF(d.lifespan.y) };
        j["spawnDelay"] = { SafeF(d.spawnDelay.x),   SafeF(d.spawnDelay.y) };
        j["fadeDuration"] = { SafeF(d.fadeDuration.x), SafeF(d.fadeDuration.y) };
        j["localOffset"] = { SafeF(d.localOffset.x),  SafeF(d.localOffset.y), SafeF(d.localOffset.z) };
        j["emitStretch"] = { SafeF(d.emitStretch.x),  SafeF(d.emitStretch.y) };
        j["uvScrollSpeed"] = { SafeF(d.uvScrollSpeed.x),SafeF(d.uvScrollSpeed.y) };
        j["scale"] = { SafeF(d.scale.x),        SafeF(d.scale.y) };
        j["color"] = { SafeF(d.color.x), SafeF(d.color.y), SafeF(d.color.z), SafeF(d.color.w) };
        j["spriteGrid"] = { d.spriteGrid.x, d.spriteGrid.y };

        // スカラー (float)
        j["localOffsetY"] = SafeF(d.localOffsetY);
        j["gravity"] = SafeF(d.gravity);
        j["velocityStretch"] = SafeF(d.velocityStretch);
        j["noiseScale"] = SafeF(d.noiseScale);
        j["noiseStrength"] = SafeF(d.noiseStrength);

        // スカラー (int)
        j["colorMode"] = d.colorMode;
        j["renderMode"] = d.renderMode;
        j["spriteMode"] = d.spriteMode;
        j["maxCount"] = d.maxCount;
        j["particleType"] = d.particleType;
        j["emitMode"] = d.emitMode;
        j["burstCount"] = d.burstCount;

        // パス
        j["texturePath"] = std::string(d.texturePath);
        j["colorRampPath"] = std::string(d.colorRampPath);
    }

    // ---- json -> desc ----------------------------------------------
    void ReadDescFromJson(const json& j, ParticleEmitterDesc& d)
    {
        auto F = [&](const char* k, float& dst) { if (j.contains(k) && j[k].is_number()) dst = j[k].get<float>(); };
        auto I = [&](const char* k, int& dst) { if (j.contains(k) && j[k].is_number()) dst = j[k].get<int>(); };
        auto F2 = [&](const char* k, GpuData::float2& v) {
            if (j.contains(k) && j[k].is_array() && j[k].size() >= 2) { v.x = j[k][0].get<float>(); v.y = j[k][1].get<float>(); }
            };
        auto F3 = [&](const char* k, GpuData::float3& v) {
            if (j.contains(k) && j[k].is_array() && j[k].size() >= 3) { v.x = j[k][0].get<float>(); v.y = j[k][1].get<float>(); v.z = j[k][2].get<float>(); }
            };
        auto F4 = [&](const char* k, GpuData::float4& v) {
            if (j.contains(k) && j[k].is_array() && j[k].size() >= 4) { v.x = j[k][0].get<float>(); v.y = j[k][1].get<float>(); v.z = j[k][2].get<float>(); v.w = j[k][3].get<float>(); }
            };
        auto STR = [&](const char* k, char* dst, size_t cap) {
            if (j.contains(k) && j[k].is_string()) strcpy_s(dst, cap, j[k].get<std::string>().c_str());
            };

        F2("emitRadius", d.emitRadius);
        F2("emitSize", d.emitSize);
        F2("coneAngle", d.coneAngle);
        F2("emitSpeed", d.emitSpeed);
        F2("angularSpeed", d.angularSpeed);
        F2("lifespan", d.lifespan);
        F2("spawnDelay", d.spawnDelay);
        F2("fadeDuration", d.fadeDuration);
        F3("localOffset", d.localOffset);
        F2("emitStretch", d.emitStretch);
        F2("uvScrollSpeed", d.uvScrollSpeed);
        F2("scale", d.scale);
        F4("color", d.color);

        F("localOffsetY", d.localOffsetY);
        F("gravity", d.gravity);
        F("velocityStretch", d.velocityStretch);
        F("noiseScale", d.noiseScale);
        F("noiseStrength", d.noiseStrength);

        I("colorMode", d.colorMode);
        I("renderMode", d.renderMode);
        I("spriteMode", d.spriteMode);
        I("maxCount", d.maxCount);
        I("particleType", d.particleType);
        I("emitMode", d.emitMode);
        I("burstCount", d.burstCount);

        if (j.contains("spriteGrid") && j["spriteGrid"].is_array() && j["spriteGrid"].size() >= 2) {
            d.spriteGrid.x = j["spriteGrid"][0].get<uint32_t>();
            d.spriteGrid.y = j["spriteGrid"][1].get<uint32_t>();
        }

        STR("texturePath", d.texturePath, sizeof(d.texturePath));
        STR("colorRampPath", d.colorRampPath, sizeof(d.colorRampPath));
    }

    std::string ToRelativePath(const std::filesystem::path& absPath)
    {
        std::error_code ec;
        auto rel = std::filesystem::relative(absPath, std::filesystem::current_path(), ec);
        return (!ec && !rel.empty()) ? rel.generic_string() : absPath.generic_string();
    }

} // namespace

// =====================================================================
// ★ CustomSerialize: GPUParticleComponent の唯一のシリアライズ経路
//   シーン・Prefab・プリセット全部で使われる。
// =====================================================================
namespace ComponentMetaJson {

    template <>
    void Serialize<GPUParticleComponent>(json& j, const GPUParticleComponent& comp, SaveContext*)
    {
        WriteDescToJson(j, comp.desc);
    }

    template <>
    void Deserialize<GPUParticleComponent>(const json& j, GPUParticleComponent& comp, LoadContext*)
    {
        ReadDescFromJson(j, comp.desc);

        // パスからテクスチャ Handle を復元
        auto& rm = ResourceManager::Instance();
        comp.texture = (comp.desc.texturePath[0] != '\0') ? rm.LoadTexture(comp.desc.texturePath) : TextureHandle{};
        comp.colorRamp = (comp.desc.colorRampPath[0] != '\0') ? rm.LoadTexture(comp.desc.colorRampPath) : TextureHandle{};

        // GPU リソース再構築 + staticCB 再アップロードを要求
        comp.isInitialized = false;
        comp.needsGpuInit = true;
        comp._staticDirty = true;
        comp.burstTrigger = 0.0f;
    }

} // namespace ComponentMetaJson

// =====================================================================
// プリセット保存/読込ヘルパー (FileSystem 経由・上の Serialize を再利用)
//   → シーン保存と完全に同じスキーマになる
// =====================================================================
namespace {

    bool SavePreset(const std::string& path, const GPUParticleComponent& comp)
    {
        json j;
        ComponentMetaJson::Serialize<GPUParticleComponent>(j, comp, nullptr);
        return FileSystem::WriteAllText(path, j.dump(4));
    }

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

} // namespace

// =====================================================================
// ComponentMeta<GPUParticleComponent>
// =====================================================================
template <> struct ComponentMeta<GPUParticleComponent> {
    static constexpr bool        registered = true;
    static constexpr const char* displayName = "GPU Particle";
    static constexpr bool        hasCustomGui = true;

#ifdef ENABLE_EDITOR
    static bool CustomGui(GPUParticleComponent& compRef, unsigned long long = 0, void* = nullptr)
    {
        bool  changed = false;
        auto* p = &compRef;
        auto& d = p->desc;                 // ★ すべて desc を編集
        auto& rm = ResourceManager::Instance();
        HWND  hWnd = Graphics::Instance().GetWindowHandle();

        // ---- テクスチャ即ロード用ヘルパー ----------------------------
        auto pickTexture = [&](char* dst, size_t cap, TextureHandle& handle, const char* title) {
            if (auto path = Engine::FileDialog::OpenPreset("Texture", title, hWnd)) {
                strcpy_s(dst, cap, ToRelativePath(*path).c_str());
                handle = rm.LoadTexture(dst);     // ★ 選択時に即ロード (バッファ再構築は不要)
                changed = true;
            }
            };

        // ================================================================
        // プリセット保存 / 読込
        // ================================================================
        if (ImGui::Button(u8"プリセット保存 (Save Preset)")) {
            if (auto path = Engine::FileDialog::SavePreset("GPUParticlePreset", u8"Save Particle Preset", {}, hWnd))
                SavePreset(path->string(), *p);
        }
        ImGui::SameLine();
        if (ImGui::Button(u8"プリセット読込 (Load Preset)")) {
            if (auto path = Engine::FileDialog::OpenPreset("GPUParticlePreset", u8"Load Particle Preset", hWnd))
                if (LoadPreset(path->string(), *p)) changed = true;
        }
        ImGui::Separator();

        // ================================================================
        // システム設定 (System)
        // ================================================================
        ImGui::TextDisabled(u8"システム設定 (System)");
        ImGui::Text(u8"現在の最大数 (Max): %d", d.maxCount);

        // ★ InputInt + Enter 確定。ドラッグ中に毎フレーム再構築しないための工夫。
        int tempMax = d.maxCount;
        if (ImGui::InputInt(u8"最大許容量 (Max Capacity)", &tempMax, 0, 0)) {
            if (tempMax > 0 && tempMax != d.maxCount) {
                d.maxCount = tempMax;
                p->isInitialized = false;     // ★ maxCount 変更時のみバッファ再構築
                changed = true;
            }
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(u8"Enterキーで適用します。\n注意: パーティクルが全てリセットされます。");

        const char* typeNames[] = { "Point (Color Only)", "Quad (Texture)" };
        if (ImGui::Combo(u8"描画タイプ (Render Type)", &d.particleType, typeNames, IM_ARRAYSIZE(typeNames)))
            changed = true;
        ImGui::Spacing();

        // ================================================================
        // 発生設定 (Emission)
        // ================================================================
        if (ImGui::TreeNode(u8"発生設定 (Emission)")) {
            const char* modes[] = { "Continuous (継続)", "Burst (爆発)" };
            if (ImGui::Combo(u8"発生モード (Mode)", &d.emitMode, modes, IM_ARRAYSIZE(modes))) changed = true;

            if (d.emitMode == 1) {
                if (ImGui::DragInt(u8"発生数 (Count)", &d.burstCount, 1, 0, 10000)) changed = true;
                if (ImGui::Button(u8"発火テスト (Trigger)", ImVec2(-1, 0))) p->TriggerBurst();
            }
            else {
                if (ImGui::DragFloat2(u8"発生間隔 (Delay Min/Max)", &d.spawnDelay.x, 0.001f, 0.0f, 5.0f, "%.4f")) changed = true;
            }

            ImGui::Separator();
            ImGui::TextDisabled(u8"形状と位置 (Shape & Position)");
            if (ImGui::DragFloat3(u8"位置オフセット (Pos Offset)", &d.localOffset.x, 0.1f))                changed = true;
            if (ImGui::DragFloat(u8"高さ範囲 Y (Height Volume)", &d.localOffsetY, 0.1f, 0.0f, 50.0f))     changed = true;
            if (ImGui::DragFloat2(u8"半径 (Radius Min/Max)", &d.emitRadius.x, 0.1f))                       changed = true;
            if (ImGui::DragFloat2(u8"形状比率 XZ (Shape Stretch)", &d.emitStretch.x, 0.01f, 0.0f, 10.0f))  changed = true;
            if (ImGui::DragFloat2(u8"角度 (Angle Min/Max)", &d.coneAngle.x, 0.1f))                          changed = true;
            ImGui::TreePop();
        }

        // ================================================================
        // 挙動・物理 (Movement & Physics)
        // ================================================================
        if (ImGui::TreeNode(u8"挙動・物理 (Movement)")) {
            if (ImGui::DragFloat(u8"重力 (Gravity)", &d.gravity, 0.1f))                                  changed = true;
            if (ImGui::DragFloat2(u8"速度 (Speed Min/Max)", &d.emitSpeed.x, 0.1f))                        changed = true;
            if (ImGui::DragFloat2(u8"回転速度 (Rot Speed Min/Max)", &d.angularSpeed.x, 0.1f))             changed = true;
            if (ImGui::DragFloat2(u8"寿命 (Life Min/Max)", &d.lifespan.x, 0.1f, 0.0f, 60.0f))             changed = true;

            ImGui::Separator();
            ImGui::TextDisabled(u8"乱流・ノイズ (Turbulence)");
            if (ImGui::DragFloat(u8"ノイズ強度 (Strength)", &d.noiseStrength, 0.1f))                      changed = true;
            if (ImGui::DragFloat(u8"ノイズサイズ (Scale)", &d.noiseScale, 0.01f))                         changed = true;
            ImGui::TreePop();
        }

        // ================================================================
        // 見た目・アニメーション (Appearance)
        // ================================================================
        if (ImGui::TreeNode(u8"見た目 (Appearance)")) {
            const char* renderModes[] = { "Additive (Fire/Light)", "Alpha Blend (Smoke/Dark)" };
            if (ImGui::Combo(u8"描画ブレンド (Render Mode)", &d.renderMode, renderModes, IM_ARRAYSIZE(renderModes))) changed = true;

            if (ImGui::ColorEdit4(u8"基本色 (Base Color)", &d.color.x)) changed = true;

            const char* colorModes[] = { u8"単色 (Single Color)", u8"カラーランプ (Color Ramp)" };
            if (ImGui::Combo(u8"カラーモード (Color Mode)", &d.colorMode, colorModes, IM_ARRAYSIZE(colorModes))) changed = true;
            if (d.colorMode == 1) ImGui::TextDisabled(u8"※ テクスチャ項目の Color Ramp が使用されます");

            if (ImGui::DragFloat2(u8"サイズ (Size Start/End)", &d.emitSize.x, 0.01f, 0.0f, 100.0f))       changed = true;
            if (ImGui::DragFloat2(u8"全体スケール (Particle Scale)", &d.scale.x, 0.01f, 0.0f, 100.0f))    changed = true;
            if (ImGui::DragFloat2(u8"フェード (Fade In/Out)", &d.fadeDuration.x, 0.01f, 0.0f, 10.0f))     changed = true;

            ImGui::Separator();
            ImGui::TextDisabled(u8"特殊効果 (Effects)");
            if (ImGui::DragFloat(u8"速度ストレッチ (Stretch)", &d.velocityStretch, 0.01f, 0.0f, 10.0f))   changed = true;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"移動方向に引き伸ばします (0.0=無効)");
            if (ImGui::DragFloat2(u8"UVスクロール (UV Scroll)", &d.uvScrollSpeed.x, 0.01f))               changed = true;

            ImGui::Separator();
            ImGui::TextDisabled(u8"スプライトアニメ (Sprite Sheet)");
            int grid[2] = { (int)d.spriteGrid.x, (int)d.spriteGrid.y };
            if (ImGui::DragInt2(u8"分割数 (Grid X/Y)", grid, 0.1f, 1, 16)) {
                d.spriteGrid.x = (uint32_t)(grid[0] < 1 ? 1 : grid[0]);
                d.spriteGrid.y = (uint32_t)(grid[1] < 1 ? 1 : grid[1]);
                changed = true;
            }
            const char* animModes[] = { "Random (Fixed)", "Life Cycle (Animate)" };
            if (ImGui::Combo(u8"再生モード (Anim Mode)", &d.spriteMode, animModes, IM_ARRAYSIZE(animModes))) changed = true;
            ImGui::TreePop();
        }

        // ================================================================
        // テクスチャ設定 (Textures)
        // ================================================================
        if (ImGui::TreeNode(u8"テクスチャ (Textures)")) {
            // --- メインテクスチャ ---
            ImGui::TextDisabled(u8"メインテクスチャ (Main Texture)");
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 65.0f);
            if (ImGui::InputText("##texPath", d.texturePath, sizeof(d.texturePath))) changed = true;
            ImGui::SameLine();
            if (ImGui::Button(u8"読込##Main")) pickTexture(d.texturePath, sizeof(d.texturePath), p->texture, "Select Texture");

            if (ID3D11ShaderResourceView* srv = rm.GetTexture(p->texture)) {
                ImGui::Image((void*)srv, ImVec2(64, 64), ImVec2(0, 0), ImVec2(1, 1), ImVec4(1, 1, 1, 1), ImVec4(1, 1, 1, 0.5f));
                ImGui::SameLine();
                if (ImGui::Button(u8"クリア##Main")) {
                    memset(d.texturePath, 0, sizeof(d.texturePath));
                    p->texture = TextureHandle{};
                    changed = true;
                }
            }

            ImGui::Separator();

            // --- カラーランプ ---
            ImGui::TextDisabled(u8"カラーランプ (Color Ramp)");
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 65.0f);
            if (ImGui::InputText("##rampPath", d.colorRampPath, sizeof(d.colorRampPath))) changed = true;
            ImGui::SameLine();
            if (ImGui::Button(u8"読込##Ramp")) pickTexture(d.colorRampPath, sizeof(d.colorRampPath), p->colorRamp, "Select Color Ramp");

            if (ID3D11ShaderResourceView* srv = rm.GetTexture(p->colorRamp)) {
                ImGui::Image((void*)srv, ImVec2(128, 16), ImVec2(0, 0), ImVec2(1, 1), ImVec4(1, 1, 1, 1), ImVec4(1, 1, 1, 0.5f));
                ImGui::SameLine();
                if (ImGui::Button(u8"クリア##Ramp")) {
                    memset(d.colorRampPath, 0, sizeof(d.colorRampPath));
                    p->colorRamp = TextureHandle{};
                    changed = true;
                }
            }
            ImGui::TreePop();
        }

        // ★ どれか1つでも編集されたら staticCB 再アップロードを要求 (1箇所で集約)
        if (changed) p->MarkDescChanged();
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