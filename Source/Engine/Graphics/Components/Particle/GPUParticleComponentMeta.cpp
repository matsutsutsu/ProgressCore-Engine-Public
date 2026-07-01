/// @file  GPUParticleComponentMeta.cpp
/// @brief GPUParticleComponent のシリアライズ/インスペクタ定義 (タブ最適化版)。

#include "Engine/Graphics/Components/Particle/GPUParticleComponent.h"
#include "Engine/Graphics/RHI/Graphics.h"
#include "Engine/Platform/Core/Dialog.h"
#include "Engine/Platform/Core/FileSystem.h"

#include "Engine/Serialization/Meta/ComponentDefine.h"
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
// ParticleEmitterDesc <-> json 変換ロジック
// =====================================================================
namespace
{

inline float SafeF(float v)
{
    return std::isfinite(v) ? v : 0.0f;
}

void WriteDescToJson(json &j, const ParticleEmitterDesc &d)
{
    j["emitRadius"]    = {SafeF(d.emitRadius.x), SafeF(d.emitRadius.y)};
    j["emitSize"]      = {SafeF(d.emitSize.x), SafeF(d.emitSize.y)};
    j["coneAngle"]     = {SafeF(d.coneAngle.x), SafeF(d.coneAngle.y)};
    j["emitSpeed"]     = {SafeF(d.emitSpeed.x), SafeF(d.emitSpeed.y)};
    j["angularSpeed"]  = {SafeF(d.angularSpeed.x), SafeF(d.angularSpeed.y)};
    j["lifespan"]      = {SafeF(d.lifespan.x), SafeF(d.lifespan.y)};
    j["spawnDelay"]    = {SafeF(d.spawnDelay.x), SafeF(d.spawnDelay.y)};
    j["fadeDuration"]  = {SafeF(d.fadeDuration.x), SafeF(d.fadeDuration.y)};
    j["localOffset"]   = {SafeF(d.localOffset.x), SafeF(d.localOffset.y), SafeF(d.localOffset.z)};
    j["emitStretch"]   = {SafeF(d.emitStretch.x), SafeF(d.emitStretch.y)};
    j["uvScrollSpeed"] = {SafeF(d.uvScrollSpeed.x), SafeF(d.uvScrollSpeed.y)};
    j["scale"]         = {SafeF(d.scale.x), SafeF(d.scale.y)};
    j["color"]         = {SafeF(d.color.x), SafeF(d.color.y), SafeF(d.color.z), SafeF(d.color.w)};
    j["spriteGrid"]    = {d.spriteGrid.x, d.spriteGrid.y};

    j["localOffsetY"]    = SafeF(d.localOffsetY);
    j["gravity"]         = SafeF(d.gravity);
    j["velocityStretch"] = SafeF(d.velocityStretch);
    j["noiseScale"]      = SafeF(d.noiseScale);
    j["noiseStrength"]   = SafeF(d.noiseStrength);

    j["colorMode"]    = d.colorMode;
    j["renderMode"]   = d.renderMode;
    j["spriteMode"]   = d.spriteMode;
    j["maxCount"]     = d.maxCount;
    j["particleType"] = d.particleType;
    j["emitMode"]     = d.emitMode;
    j["burstCount"]   = d.burstCount;

    j["attractorPos"]      = {d.attractorPos.x, d.attractorPos.y, d.attractorPos.z};
    j["attractorStrength"] = d.attractorStrength;
    j["attractorRadius"]   = d.attractorRadius;
    j["vortexStrength"]    = d.vortexStrength;
    j["drag"]              = d.drag;
    j["attractorLocal"]    = d.attractorLocal;

    j["texturePath"]   = std::string(d.texturePath);
    j["colorRampPath"] = std::string(d.colorRampPath);
}

void ReadDescFromJson(const json &j, ParticleEmitterDesc &d)
{
    auto F  = [&](const char *k, float &dst) { if (j.contains(k) && j[k].is_number()) dst = j[k].get<float>(); };
    auto I  = [&](const char *k, int &dst) { if (j.contains(k) && j[k].is_number()) dst = j[k].get<int>(); };
    auto F2 = [&](const char *k, GpuData::float2 &v) {
        if (j.contains(k) && j[k].is_array() && j[k].size() >= 2)
        {
            v.x = j[k][0].get<float>();
            v.y = j[k][1].get<float>();
        }
    };
    auto F3 = [&](const char *k, GpuData::float3 &v) {
        if (j.contains(k) && j[k].is_array() && j[k].size() >= 3)
        {
            v.x = j[k][0].get<float>();
            v.y = j[k][1].get<float>();
            v.z = j[k][2].get<float>();
        }
    };
    auto F4 = [&](const char *k, GpuData::float4 &v) {
        if (j.contains(k) && j[k].is_array() && j[k].size() >= 4)
        {
            v.x = j[k][0].get<float>();
            v.y = j[k][1].get<float>();
            v.z = j[k][2].get<float>();
            v.w = j[k][3].get<float>();
        }
    };
    auto STR = [&](const char *k, char *dst, size_t cap) {
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
    d.attractorLocal = j.value("attractorLocal", true);

    if (j.contains("spriteGrid") && j["spriteGrid"].is_array() && j["spriteGrid"].size() >= 2)
    {
        d.spriteGrid.x = j["spriteGrid"][0].get<uint32_t>();
        d.spriteGrid.y = j["spriteGrid"][1].get<uint32_t>();
    }

    F3("attractorPos", d.attractorPos);
    F("attractorStrength", d.attractorStrength);
    F("attractorRadius", d.attractorRadius);
    F("vortexStrength", d.vortexStrength);
    F("drag", d.drag);

    STR("texturePath", d.texturePath, sizeof(d.texturePath));
    STR("colorRampPath", d.colorRampPath, sizeof(d.colorRampPath));
}

std::string ToRelativePath(const std::filesystem::path &absPath)
{
    std::error_code ec;
    auto            rel = std::filesystem::relative(absPath, std::filesystem::current_path(), ec);
    return (!ec && !rel.empty()) ? rel.generic_string() : absPath.generic_string();
}
} // namespace

// =====================================================================
// CustomSerialize
// =====================================================================
namespace ComponentMetaJson
{
template <>
struct ComponentSerializer<GPUParticleComponent>
{
    static void Save(json &j, const GPUParticleComponent &comp, SaveContext * /*ctx*/)
    {
        WriteDescToJson(j, comp.desc);
    }

    static void Load(const json &j, GPUParticleComponent &comp, LoadContext * /*ctx*/)
    {
        ReadDescFromJson(j, comp.desc);

        auto &rm       = ResourceManager::Instance();
        comp.texture   = (comp.desc.texturePath[0] != '\0') ? rm.LoadTexture(comp.desc.texturePath) : TextureHandle{};
        comp.colorRamp = (comp.desc.colorRampPath[0] != '\0') ? rm.LoadTexture(comp.desc.colorRampPath) : TextureHandle{};

        comp.isInitialized = false;
        comp.needsGpuInit  = true;
        comp._staticDirty  = true;
        comp.burstTrigger  = 0.0f;
    }
};
} // namespace ComponentMetaJson

// =====================================================================
// プリセット保存/読込ヘルパー
// =====================================================================
namespace
{
bool SavePreset(const std::string &path, const GPUParticleComponent &comp)
{
    json j;
    ComponentMetaJson::ComponentSerializer<GPUParticleComponent>::Save(j, comp, nullptr);
    return FileSystem::WriteAllText(path, j.dump(4));
}

bool LoadPreset(const std::string &path, GPUParticleComponent &comp)
{
    if (!FileSystem::Exists(path)) return false;
    const std::string text = FileSystem::ReadAllText(path);
    if (text.empty()) return false;
    try
    {
        json j = json::parse(text);
        ComponentMetaJson::ComponentSerializer<GPUParticleComponent>::Load(j, comp, nullptr);
        return true;
    }
    catch (...)
    {
        return false;
    }
}
} // namespace

// =====================================================================
// ComponentMeta<GPUParticleComponent>
// =====================================================================
template <>
struct ComponentMeta<GPUParticleComponent>
{
    CCL_META_USING_THIS(GPUParticleComponent)

    static constexpr const char *displayName    = "GPU Particle Emitter";
    static constexpr bool        hasCustomGui   = true;
    static constexpr bool        isSerializable = true;

#ifdef ENABLE_EDITOR
    static bool CustomGui(GPUParticleComponent &compRef, unsigned long long /*entityID*/ = 0, void * /*worldPtr*/ = nullptr)
    {
        bool  changed = false;
        auto *p       = &compRef;
        auto &d       = p->desc;
        auto &rm      = ResourceManager::Instance();
        HWND  hWnd    = Graphics::Instance().GetWindowHandle();

        // 静的ローカル変数で「現在どっちのタブが開かれているか」を記憶する
        static int lastActiveTabId    = -1;
        int        currentActiveTabId = -1;

        auto pickTexture = [&](char *dst, size_t cap, TextureHandle &handle, const char *title) {
            if (auto path = FileDialog::OpenPreset("Texture", title, hWnd))
            {
                strcpy_s(dst, cap, ToRelativePath(*path).c_str());
                handle  = rm.LoadTexture(dst);
                changed = true;
            }
        };

        if (ImGui::Button(u8"プリセット保存 (Save Preset)"))
        {
            if (auto path = FileDialog::SavePreset("GPUParticlePreset", u8"Save Particle Preset", {}, hWnd))
                SavePreset(path->string(), *p);
        }
        ImGui::SameLine();
        if (ImGui::Button(u8"プリセット読込 (Load Preset)"))
        {
            if (auto path = FileDialog::OpenPreset("GPUParticlePreset", u8"Load Particle Preset", hWnd))
                if (LoadPreset(path->string(), *p)) changed = true;
        }
        ImGui::Separator();

        // --------------------------------────────────────-----------------
        // 🌟 改善の核心: BeginChild による「独立したスクロール宇宙」の確立
        // --------------------------------────────────────────────────────-
        // 高さを 380px に完全固定します。これにより、どんなにタブを切り替えても
        // インスペクタ（親）の全体の高さがガタガタ動くのを物理的に阻止します。
        // 第4引数に ImGuiWindowFlags_NavFlattened を指定して操作性を確保します。
        ImGui::BeginChild("ParticleEditorChild", ImVec2(0, 380.0f), ImGuiChildFlags_Border, ImGuiWindowFlags_NavFlattened);

        // -----------------------------------------------------------------
        // 🌟 タブバーによる認知的負荷の分散とスクロールの撲滅
        // -----------------------------------------------------------------
        if (ImGui::BeginTabBar("ParticleTabs"))
        {

            // =============================================================
            // TAB 1: 基礎設定 ＆ 発生形状
            // =============================================================
            if (ImGui::BeginTabItem(u8"基本・発生 (Base & Spawn)"))
            {
                currentActiveTabId = 0; // IDを割り当て
                ImGui::Spacing();
                ImGui::SeparatorText(u8"エミッタ設定 (Emitter)");
                int tempMax = d.maxCount;
                if (ImGui::DragInt(u8"最大許容量 (Max Capacity)", &tempMax, 10.0f, 1, 100000))
                {
                    if (tempMax > 0 && tempMax != d.maxCount)
                    {
                        d.maxCount       = tempMax;
                        p->isInitialized = false;
                        changed          = true;
                    }
                }
                const char *typeNames[] = {"Point (Color Only)", "Quad (Texture)"};
                if (ImGui::Combo(u8"描画タイプ (Render Type)", &d.particleType, typeNames, IM_ARRAYSIZE(typeNames))) changed = true;

                const char *modes[] = {"Continuous (継続)", "Burst (爆発)"};
                if (ImGui::Combo(u8"発生モード (Mode)", &d.emitMode, modes, IM_ARRAYSIZE(modes))) changed = true;

                if (d.emitMode == 1)
                { // Burst
                    if (ImGui::DragInt(u8"発生数 (Count)", &d.burstCount, 1, 0, 10000)) changed = true;
                    if (ImGui::Button(u8"発火テスト (Trigger)", ImVec2(-1, 0))) p->TriggerBurst();
                }
                else
                { // Continuous
                    // 🌟 工夫: Min/Max の2変数を DragFloat2 で横一列に圧縮し、スクロールを削減
                    if (ImGui::DragFloat2(u8"発生間隔 (Delay Min/Max)", &d.spawnDelay.x, 0.001f, 0.0f, 5.0f, "%.4f"))
                    {
                        if (d.spawnDelay.x > d.spawnDelay.y) d.spawnDelay.y = d.spawnDelay.x;
                        changed = true;
                    }
                }

                ImGui::SeparatorText(u8"形状と位置 (Shape & Position)");
                changed |= ImGui::DragFloat3(u8"位置オフセット (Pos Offset)", &d.localOffset.x, 0.1f);
                changed |= ImGui::DragFloat(u8"高さ範囲 Y (Height Volume)", &d.localOffsetY, 0.1f, 0.0f, 50.0f);
                if (ImGui::DragFloat2(u8"半径 (Radius Min/Max)", &d.emitRadius.x, 0.01f, 0.0f, 100.0f, "%.2f"))
                {
                    if (d.emitRadius.x > d.emitRadius.y) d.emitRadius.y = d.emitRadius.x;
                    changed = true;
                }
                changed |= ImGui::DragFloat2(u8"形状比率 XZ (Stretch)", &d.emitStretch.x, 0.01f, 0.0f, 10.0f, "%.2f");
                if (ImGui::DragFloat2(u8"放出角度 (Cone Angle Min/Max)", &d.coneAngle.x, 0.01f, 0.0f, 3.1415f, "%.2f rad"))
                {
                    if (d.coneAngle.x > d.coneAngle.y) d.coneAngle.y = d.coneAngle.x;
                    changed = true;
                }

                ImGui::EndTabItem();
            }

            // =============================================================
            // TAB 2: 運動 ＆ 物理
            // =============================================================
            if (ImGui::BeginTabItem(u8"運動・物理 (Movement)"))
            {
                currentActiveTabId = 1; // IDを割り当て
                ImGui::Spacing();
                ImGui::SeparatorText(u8"速度 (Velocity)");
                changed |= ImGui::DragFloat2(u8"初速度 (Speed Min/Max)", &d.emitSpeed.x, 0.1f, -100.0f, 100.0f, "%.2f");
                changed |= ImGui::DragFloat2(u8"回転速度 (Rot Speed Min/Max)", &d.angularSpeed.x, 0.1f, -100.0f, 100.0f, "%.2f");
                changed |= ImGui::DragFloat(u8"重力 (Gravity)", &d.gravity, 0.1f);

                ImGui::SeparatorText(u8"乱流・ノイズ (Turbulence)");
                changed |= ImGui::DragFloat(u8"ノイズ強度 (Strength)", &d.noiseStrength, 0.1f);
                changed |= ImGui::DragFloat(u8"ノイズサイズ (Scale)", &d.noiseScale, 0.01f);

                ImGui::SeparatorText(u8"引力・斥力 (Force Field)");
                changed |= ImGui::Checkbox(u8"エミッタ相対空間 (Local Space)", &d.attractorLocal);
                changed |= ImGui::DragFloat3(u8"中心位置 (Center)", &d.attractorPos.x, 0.05f);
                changed |= ImGui::DragFloat(u8"強度 (+:引力 -:斥力)", &d.attractorStrength, 0.1f);
                changed |= ImGui::DragFloat(u8"影響半径 (Radius)", &d.attractorRadius, 0.05f, 0.0f, 100.0f);
                changed |= ImGui::DragFloat(u8"渦の強さ (Vortex)", &d.vortexStrength, 0.1f);
                changed |= ImGui::DragFloat(u8"空気抵抗・減衰 (Drag)", &d.drag, 0.05f, 0.0f, 20.0f);

                ImGui::EndTabItem();
            }

            // =============================================================
            // TAB 3: サイズ ＆ 寿命変化
            // =============================================================
            if (ImGui::BeginTabItem(u8"サイズ・寿命 (Size & Life)"))
            {
                currentActiveTabId = 2; // IDを割り当て
                ImGui::Spacing();
                ImGui::SeparatorText(u8"寿命 (Lifespan)");
                if (ImGui::DragFloat2(u8"生存時間 (Life Min/Max)", &d.lifespan.x, 0.05f, 0.01f, 60.0f, "%.2f sec"))
                {
                    if (d.lifespan.x > d.lifespan.y) d.lifespan.y = d.lifespan.x;
                    changed = true;
                }

                ImGui::SeparatorText(u8"サイズ・スケール (Size & Scale)");
                changed |= ImGui::DragFloat2(u8"時間変化 (Size Start/End)", &d.emitSize.x, 0.01f, 0.0f, 100.0f, "%.2f");
                changed |= ImGui::DragFloat2(u8"全体スケール (Global Scale)", &d.scale.x, 0.01f, 0.0f, 100.0f, "%.2f");
                changed |= ImGui::DragFloat2(u8"フェード (Fade In/Out sec)", &d.fadeDuration.x, 0.01f, 0.0f, 10.0f, "%.2f");

                ImGui::EndTabItem();
            }

            // =============================================================
            // TAB 4: グラフィック ＆ テクスチャ参照
            // =============================================================
            if (ImGui::BeginTabItem(u8"描画・テクスチャ (Graphics)"))
            {
                currentActiveTabId = 3; // IDを割り当て
                ImGui::Spacing();
                ImGui::SeparatorText(u8"カラー・ブレンド (Color & Blend)");
                const char *renderModes[] = {"Additive (Fire/Light)", "Alpha Blend (Smoke/Dark)"};
                if (ImGui::Combo(u8"描画ブレンド (Render Mode)", &d.renderMode, renderModes, IM_ARRAYSIZE(renderModes))) changed = true;

                const char *colorModes[] = {u8"単色 (Single Color)", u8"カラーランプ (Color Ramp)"};
                if (ImGui::Combo(u8"カラーモード (Color Mode)", &d.colorMode, colorModes, IM_ARRAYSIZE(colorModes))) changed = true;

                if (d.colorMode == 0)
                {
                    changed |= ImGui::ColorEdit4(u8"基本色 (Base Color)", &d.color.x);
                }
                else
                {
                    ImGui::TextDisabled(u8"※ 下記の Color Ramp テクスチャが使用されます");
                }

                ImGui::SeparatorText(u8"特殊効果 (Effects)");
                changed |= ImGui::DragFloat(u8"速度ストレッチ (Velocity Stretch)", &d.velocityStretch, 0.01f, 0.0f, 10.0f);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"移動方向に引き伸ばします (0.0=無効)");
                changed |= ImGui::DragFloat2(u8"UVスクロール (UV Scroll Speed)", &d.uvScrollSpeed.x, 0.01f);

                ImGui::SeparatorText(u8"スプライトアニメ (Sprite Sheet)");
                int grid[2] = {(int)d.spriteGrid.x, (int)d.spriteGrid.y};
                if (ImGui::DragInt2(u8"分割数 (Grid X/Y)", grid, 0.1f, 1, 16))
                {
                    d.spriteGrid.x = (uint32_t)(grid[0] < 1 ? 1 : grid[0]);
                    d.spriteGrid.y = (uint32_t)(grid[1] < 1 ? 1 : grid[1]);
                    changed        = true;
                }
                const char *animModes[] = {"Random (Fixed)", "Life Cycle (Animate)"};
                if (ImGui::Combo(u8"再生モード (Anim Mode)", &d.spriteMode, animModes, IM_ARRAYSIZE(animModes))) changed = true;

                ImGui::SeparatorText(u8"テクスチャ (Textures)");
                ImGui::TextDisabled(u8"メインテクスチャ (Main Texture)");
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 65.0f);
                if (ImGui::InputText("##texPath", d.texturePath, sizeof(d.texturePath))) changed = true;
                ImGui::SameLine();
                if (ImGui::Button(u8"読込##Main")) pickTexture(d.texturePath, sizeof(d.texturePath), p->texture, "Select Texture");

                if (ID3D11ShaderResourceView *srv = rm.GetTexture(p->texture))
                {
                    ImGui::Image((void *)srv, ImVec2(64, 64), ImVec2(0, 0), ImVec2(1, 1), ImVec4(1, 1, 1, 1), ImVec4(1, 1, 1, 0.5f));
                    ImGui::SameLine();
                    if (ImGui::Button(u8"クリア##Main"))
                    {
                        memset(d.texturePath, 0, sizeof(d.texturePath));
                        p->texture = TextureHandle{};
                        changed    = true;
                    }
                }

                ImGui::Spacing();
                ImGui::TextDisabled(u8"カラーランプ (Color Ramp)");
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 65.0f);
                if (ImGui::InputText("##rampPath", d.colorRampPath, sizeof(d.colorRampPath))) changed = true;
                ImGui::SameLine();
                if (ImGui::Button(u8"読込##Ramp")) pickTexture(d.colorRampPath, sizeof(d.colorRampPath), p->colorRamp, "Select Color Ramp");

                if (ID3D11ShaderResourceView *srv = rm.GetTexture(p->colorRamp))
                {
                    ImGui::Image((void *)srv, ImVec2(128, 16), ImVec2(0, 0), ImVec2(1, 1), ImVec4(1, 1, 1, 1), ImVec4(1, 1, 1, 0.5f));
                    ImGui::SameLine();
                    if (ImGui::Button(u8"クリア##Ramp"))
                    {
                        memset(d.colorRampPath, 0, sizeof(d.colorRampPath));
                        p->colorRamp = TextureHandle{};
                        changed      = true;
                    }
                }

                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }

        // 🌟 自動スクロールリセットの魔法: タブの切り替えが発生した瞬間、
        // この ChildWindow のスクロール位置を一番上（0.0f）に強制リセットする。
        if (currentActiveTabId != lastActiveTabId && currentActiveTabId != -1)
        {
            ImGui::SetScrollY(0.0f);
            lastActiveTabId = currentActiveTabId;
        }

        ImGui::EndChild(); // スクロール宇宙の終了

        // 値が変更されていればGPU側のバッファ更新フラグを立てる
        if (changed) p->MarkDescChanged();

        return changed;
    }
#else
    static bool CustomGui(GPUParticleComponent &, unsigned long long, void *)
    {
        return false;
    }
#endif

    // シリアライズ用の定義（自動シリアライズ側は JSON でカスタム処理しているため空でOK）
    static const std::vector<FieldDescriptor> &Fields()
    {
        static const std::vector<FieldDescriptor> empty;
        return empty;
    }
};

REGISTER_COMPONENT(GPUParticleComponent, "GPUParticle")
REGISTER_COMPONENT_EDITOR_GUI(GPUParticleComponent)