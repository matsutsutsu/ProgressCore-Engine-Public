/// @file  WandBuildComponentMeta.cpp
/// @brief WandBuildComponent のシリアライズ定義とインスペクタGUI。
///        char配列(bulletVfxPath)と2次元配列(slots/slotCounts)を含むため
///        パターンC(ComponentSerializer<T>特殊化)でシリアライズを手書きする。
///        インスペクタはパターンBのカスタムGUIで提供する。

#include "Game/Logics/Wand/Compiler/WandBuildComponent.h"
#include "Engine/Serialization/Meta/ComponentDefine.h"
#include "Engine/Serialization/Meta/ComponentMetaJson.h"
#include <json.hpp>

#ifdef ENABLE_EDITOR
#include "Editor/Inspector/ComponentGuiRegistry.h"
#include "Engine/Serialization/Meta/ComponentMetaImGui.h"
#include "Engine/Platform/Core/Dialog.h"
#endif

// ===========================================================================
// (1) カスタムシリアライズ (パターンC)
//     char[] と多次元配列は CFIELD 推論不可のため ComponentSerializer で手書きする。
// ===========================================================================
namespace ComponentMetaJson {
    template <>
    struct ComponentSerializer<WandBuildComponent> {

        static void Save(json& j, const WandBuildComponent& c, SaveContext* /*ctx*/)
        {
            // --- スカラ値 ---
            j["baseLifetime"] = c.baseLifetime;
            j["baseSpeed"] = c.baseSpeed;
            j["cooldown"] = c.cooldown;
            j["baseDamage"] = c.baseDamage;
            j["baseKnockback"] = c.baseKnockback;
            j["baseCritRate"] = c.baseCritRate;
            j["baseCritMult"] = c.baseCritMult;

            // --- char配列 (bulletVfxPath) ---
            j["bulletVfxPath"] = std::string(c.bulletVfxPath);

            // --- slotCounts (uint8_t 配列) ---
            {
                json arr = json::array();
                for (int p = 0; p < static_cast<int>(WandPhase::Count); ++p)
                    arr.push_back(c.slotCounts[p]);
                j["slotCounts"] = arr;
            }

            // --- slots (2次元配列 [Phase][Slot] → EffectType を int として保存) ---
            {
                json phases = json::array();
                for (int p = 0; p < static_cast<int>(WandPhase::Count); ++p)
                {
                    json row = json::array();
                    for (int s = 0; s < static_cast<int>(MAX_SLOTS_PER_PHASE); ++s)
                        row.push_back(static_cast<int>(c.slots[p][s]));
                    phases.push_back(row);
                }
                j["slots"] = phases;
            }
        }

        static void Load(const json& j, WandBuildComponent& c, LoadContext* /*ctx*/)
        {
            // --- スカラ値 ---
            if (j.contains("baseLifetime"))  c.baseLifetime = j["baseLifetime"].get<float>();
            if (j.contains("baseSpeed"))     c.baseSpeed = j["baseSpeed"].get<float>();
            if (j.contains("cooldown"))      c.cooldown = j["cooldown"].get<float>();
            if (j.contains("baseDamage"))    c.baseDamage = j["baseDamage"].get<float>();
            if (j.contains("baseKnockback")) c.baseKnockback = j["baseKnockback"].get<float>();
            if (j.contains("baseCritRate"))  c.baseCritRate = j["baseCritRate"].get<float>();
            if (j.contains("baseCritMult"))  c.baseCritMult = j["baseCritMult"].get<float>();

            // --- char配列 (bulletVfxPath) ---
            if (j.contains("bulletVfxPath"))
            {
                const std::string s = j["bulletVfxPath"].get<std::string>();
                strncpy_s(c.bulletVfxPath, sizeof(c.bulletVfxPath), s.c_str(), _TRUNCATE);
            }

            // --- slotCounts ---
            if (j.contains("slotCounts") && j["slotCounts"].is_array())
            {
                const auto& arr = j["slotCounts"];
                for (int p = 0; p < static_cast<int>(WandPhase::Count) && p < (int)arr.size(); ++p)
                    c.slotCounts[p] = arr[p].get<uint8_t>();
            }

            // --- slots ---
            if (j.contains("slots") && j["slots"].is_array())
            {
                const auto& phases = j["slots"];
                for (int p = 0; p < static_cast<int>(WandPhase::Count) && p < (int)phases.size(); ++p)
                {
                    if (!phases[p].is_array()) continue;
                    const auto& row = phases[p];
                    for (int s = 0; s < static_cast<int>(MAX_SLOTS_PER_PHASE) && s < (int)row.size(); ++s)
                        c.slots[p][s] = static_cast<EffectType>(row[s].get<int>());
                }
            }

            // ロード後は必ず再コンパイルさせる。
            c.isDirty = true;
        }
    };
} // namespace ComponentMetaJson

// ===========================================================================
// (2) インスペクタGUI (パターンB) + ComponentMeta<T> 本体
//     Fields() は空にする(自動ループは使わない。シリアライズはComponentSerializer)。
// ===========================================================================
namespace {
#ifdef ENABLE_EDITOR
    const char* const kEffectNames[] = {
        "None", "Accelerate", "Slow", "Focus", "Homing",
        "Explosion", "Split", "Reflection", "Chaos"
    };
    const char* const kPhaseNames[] = { "Spawn", "Travel", "OnHit", "Inheritance" };
#endif
} // namespace

template <> struct ComponentMeta<WandBuildComponent>
{
    CCL_META_USING_THIS(WandBuildComponent)
        static constexpr const char* displayName = "Wand Build";
    static constexpr bool        hasCustomGui = true;
    static constexpr bool        isSerializable = true;

#ifdef ENABLE_EDITOR
    static bool CustomGui(WandBuildComponent& comp,
        unsigned long long /*entityID*/ = 0, void* /*world*/ = nullptr)
    {
        bool changed = false;

        // ---------------------------------------------------------------
        // 飛翔・寿命の基礎値
        // ---------------------------------------------------------------
        ImGui::SeparatorText("Flight");
        changed |= ImGui::DragFloat("Base Speed", &comp.baseSpeed, 0.1f, 0.0f, 1000.0f);
        changed |= ImGui::DragFloat("Base Lifetime", &comp.baseLifetime, 0.05f, 0.0f, 60.0f);
        changed |= ImGui::DragFloat("Cooldown", &comp.cooldown, 0.01f, 0.0f, 10.0f);

        // ---------------------------------------------------------------
        // 戦闘の基礎値
        // ---------------------------------------------------------------
        ImGui::SeparatorText("Combat");
        changed |= ImGui::DragFloat("Base Damage", &comp.baseDamage, 0.5f, 0.0f, 9999.0f);
        changed |= ImGui::DragFloat("Base Knockback", &comp.baseKnockback, 0.1f, 0.0f, 100.0f);
        changed |= ImGui::DragFloat("Crit Rate", &comp.baseCritRate, 0.01f, 0.0f, 1.0f, "%.2f");
        changed |= ImGui::DragFloat("Crit Mult", &comp.baseCritMult, 0.05f, 1.0f, 10.0f, "%.2fx");

        // ---------------------------------------------------------------
        // 見た目VFX (Prefabパス選択)
        // ---------------------------------------------------------------
        ImGui::SeparatorText("Visual");

        if (ImGui::InputText("VFX Path", comp.bulletVfxPath, sizeof(comp.bulletVfxPath)))
            changed = true;

        ImGui::SameLine();
        if (ImGui::Button("...##vfx"))
        {
            if (auto path = Engine::FileDialog::OpenPreset("Prefab", "Select Bullet VFX Prefab"))
            {
                const std::string rel = Engine::FileDialog::ConvertToRelativePath(*path);
                strncpy_s(comp.bulletVfxPath, sizeof(comp.bulletVfxPath),
                    rel.c_str(), _TRUNCATE);
                changed = true;
            }
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Open file dialog to select a particle prefab (.json)");

        ImGui::SameLine();
        if (ImGui::Button("x##vfx"))
        {
            comp.bulletVfxPath[0] = '\0';
            changed = true;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Clear VFX path (no visual effect)");

        if (comp.bulletVfxPath[0] == '\0')
            ImGui::TextDisabled("  (no VFX assigned)");

        // ---------------------------------------------------------------
        // アタッチメントスロット
        // ---------------------------------------------------------------
        ImGui::SeparatorText("Attachments");
        for (int p = 0; p < static_cast<int>(WandPhase::Count); ++p)
        {
            ImGui::PushID(p);
            ImGui::TextUnformatted(kPhaseNames[p]);

            int count = comp.slotCounts[p];
            if (ImGui::SliderInt("Slots", &count, 0, static_cast<int>(MAX_SLOTS_PER_PHASE)))
            {
                comp.slotCounts[p] = static_cast<uint8_t>(count);
                changed = true;
            }

            for (int s = 0; s < comp.slotCounts[p]; ++s)
            {
                ImGui::PushID(s);
                int cur = static_cast<int>(comp.slots[p][s]);
                if (ImGui::Combo("##slot", &cur, kEffectNames, 9))
                {
                    comp.slots[p][s] = static_cast<EffectType>(cur);
                    changed = true;
                }
                ImGui::PopID();
            }
            ImGui::PopID();
            ImGui::Separator();
        }

        // ---------------------------------------------------------------
        // コンパイル状態（読み取り専用）
        // ---------------------------------------------------------------
        if (comp.cachedProgramId == InvalidProgramID)
            ImGui::TextDisabled("ProgramID: (uncompiled)");
        else
            ImGui::Text("ProgramID: %u", comp.cachedProgramId);

        if (comp.isDirty)
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "* Pending recompile");

        if (changed) comp.isDirty = true;
        return changed;
    }
#else
    static bool CustomGui(WandBuildComponent&, unsigned long long, void*) { return false; }
#endif

    // Fields() は空にする。シリアライズは ComponentSerializer<WandBuildComponent> が担う。
    static const std::vector<FieldDescriptor>& Fields()
    {
        static const std::vector<FieldDescriptor> empty;
        return empty;
    }
};

REGISTER_COMPONENT(WandBuildComponent, "Wand Build")
REGISTER_COMPONENT_EDITOR_GUI(WandBuildComponent)