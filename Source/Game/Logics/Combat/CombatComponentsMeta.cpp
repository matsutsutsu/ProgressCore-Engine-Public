/// @file  CombatComponentsMeta.cpp
/// @brief 戦闘関連コンポーネントのシリアライズ/インスペクタ定義。

#include "Engine/ECS/Core/CCL_World.h"
#include "Game/Logics/Combat/CombatComponents.h"
#include "Game/Logics/Combat/CombatRosterComponent.h"
#include "Game/Logics/Combat/HealthComponent.h"
#include "Engine/Transform/Components/BoneAttachmentComponent.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/Transform/Components/PendingParentComponent.h"
#include "Engine/Graphics/Components/Core/ModelComponent.h"
#include "Engine/Graphics/Resource/Model.h"

#include "Engine/Serialization/Meta/ComponentDefine.h"
#include "Engine/Serialization/Meta/ComponentMetaJson.h"
#include "Engine/Serialization/Meta/ComponentCategories.h" // ★追加

#ifdef ENABLE_EDITOR
#include "Editor/Inspector/ComponentGuiRegistry.h"
#include "Engine/Serialization/Meta/ComponentMetaImGui.h"
#endif

#include <json.hpp>
using json = nlohmann::json;

// ===========================================================================
// 1. Hitbox / Hurtbox
// ★修正: Combat カテゴリへの明示的配置
// ===========================================================================
COMPONENT_META_BEGIN_FULL(HitboxComponent, "Hitbox", CCL::Categories::Combat, 170, false)
    CFIELD_FLOAT_RANGED(damageAmount, "Damage Amount", 1.0f, 0.0f, 9999.0f, "Base")
    CFIELD(isActive, "Is Active", "State")
    CFIELD_FLOAT_RANGED(knockback, "Knockback Force", 0.1f, 0.0f, 500.0f, "Combat Stats")
    CFIELD_FLOAT_RANGED(hitStopDuration, "HitStop Duration (s)", 0.01f, 0.0f, 5.0f, "Combat Stats")
    CFIELD_FLOAT_RANGED(hitStopScale, "HitStop Time Scale", 0.01f, 0.0f, 1.0f, "Combat Stats")
COMPONENT_META_END(HitboxComponent)

COMPONENT_META_BEGIN_FULL(HurtboxComponent, "Hurtbox", CCL::Categories::Combat, 171, false)
    CFIELD_FLOAT_RANGED(damageMultiplier, "Damage Multiplier", 0.05f, 0.0f, 10.0f, "Defense")
COMPONENT_META_END(HurtboxComponent)

// ===================================================================
// 2. BoneAttachmentComponent
// ===================================================================
template <> struct ComponentMeta<BoneAttachmentComponent> {
    CCL_META_USING_THIS(BoneAttachmentComponent)

    static constexpr const char* displayName    = "Bone Attachment (ボーン追従)";
    static constexpr const char* category       = CCL::Categories::Hierarchy; // ★追加
    static constexpr int         sortPriority   = 5; // ★追加: Transformの直後にする
    static constexpr bool        hasCustomGui   = true;
    static constexpr bool        isSerializable = true;

#ifdef ENABLE_EDITOR
    static bool CustomGui(BoneAttachmentComponent& comp, unsigned long long entityID, void* worldPtr) {
        bool changed = false;
        auto* world = static_cast<CCL::ECS::Core::World*>(worldPtr);
        if (!world) return false;

        CCL::ECS::EntityID parentID = 0;
        if (auto* trans = world->GetComponent<TransformComponent>(entityID)) {
            parentID = trans->parent;
        }
        if (parentID == 0) {
            if (auto* pending = world->GetComponent<PendingParentComponent>(entityID)) {
                parentID = pending->parentID;
            }
        }

        ImGui::Text("Target Bone Name:");
        ImGui::SetNextItemWidth(-1);

        if (parentID != 0) {
            if (auto* modelComp = world->GetComponent<ModelComponent>(parentID)) {
                if (Model* model = modelComp->GetModel()) {
                    const auto& nodes = model->GetNodes();
                    if (!nodes.empty()) {
                        const char* currentPreview = !comp.boneName.empty() ? comp.boneName.c_str() : "Select Bone...";
                        if (ImGui::BeginCombo("##BoneCombo", currentPreview)) {
                            if (ImGui::Selectable("None", comp.boneName.empty())) {
                                comp.boneName = "";
                                comp.cachedBoneIndex = -1;
                                changed = true;
                            }
                            for (int i = 0; i < nodes.size(); ++i) {
                                bool isSelected = (comp.boneName == nodes[i].name);
                                if (ImGui::Selectable(nodes[i].name.c_str(), isSelected)) {
                                    comp.boneName = nodes[i].name;
                                    comp.cachedBoneIndex = -1;
                                    changed = true;
                                }
                                if (isSelected) ImGui::SetItemDefaultFocus();
                            }
                            ImGui::EndCombo();
                        }
                        return changed;
                    }
                }
            }
        }

        ImGui::TextColored(ImVec4(1, 1, 0, 1), "[Warning] Parent Model not found.");

        char buffer[256];
        strncpy_s(buffer, sizeof(buffer), comp.boneName.c_str(), _TRUNCATE);
        if (ImGui::InputText("##BoneNameInput", buffer, sizeof(buffer))) {
            comp.boneName = buffer;
            comp.cachedBoneIndex = -1;
            changed = true;
        }
        return changed;
    }
#else
    static bool CustomGui(BoneAttachmentComponent&, unsigned long long, void*) { return false; }
#endif

    static const std::vector<FieldDescriptor>& Fields() {
        static const std::vector<FieldDescriptor> fields = {
            CFIELD(boneName, "Bone Name", "BoneAttachment")
        };
        return fields;
    }
};
REGISTER_COMPONENT(BoneAttachmentComponent, "BoneAttachmentComponent")
REGISTER_COMPONENT_EDITOR_GUI(BoneAttachmentComponent)

// ===================================================================
// パターンC: CombatRoster / Health
// ===================================================================
namespace ComponentMetaJson {
    template <> struct ComponentSerializer<CombatRosterComponent> {
        static void Save(json& j, const CombatRosterComponent& comp, SaveContext* ctx) {
            j["count"] = comp.count;
            j["entries"] = json::array();
            for (int i = 0; i < comp.count; ++i) {
                json entry;
                entry["tag"] = comp.entries[i].tag;
                entry["id"] = ctx ? ctx->GetOrRegister(comp.entries[i].id)
                    : static_cast<uint64_t>(comp.entries[i].id);
                j["entries"].push_back(entry);
            }
        }
        static void Load(const json& j, CombatRosterComponent& comp, LoadContext* ctx) {
            if (j.contains("count")) {
                comp.count = j["count"].get<int>();
                if (comp.count > 4) comp.count = 4;
            }
            if (j.contains("entries") && j["entries"].is_array()) {
                int idx = 0;
                for (const auto& entry : j["entries"]) {
                    if (idx >= comp.count) break;
                    if (entry.contains("tag")) {
                        std::string tag = entry["tag"].get<std::string>();
                        strncpy_s(comp.entries[idx].tag, sizeof(comp.entries[idx].tag), tag.c_str(), _TRUNCATE);
                    }
                    if (entry.contains("id")) {
                        uint64_t rawId = entry["id"].get<uint64_t>();
                        comp.entries[idx].id = ctx ? ctx->Resolve(rawId)
                            : static_cast<CCL::ECS::EntityID>(rawId);
                    }
                    ++idx;
                }
            }
        }
    };

    template <> struct ComponentSerializer<HealthComponent> {
        static void Save(json& j, const HealthComponent& comp, SaveContext* /*ctx*/) {
            j["maxHealth"] = comp.maxHealth;
            j["currentHealth"] = comp.currentHealth;
            j["invincibilityDuration"] = comp.invincibilityDuration;
            j["team"] = static_cast<int>(comp.team);
        }
        static void Load(const json& j, HealthComponent& comp, LoadContext* /*ctx*/) {
            if (j.contains("maxHealth"))             comp.maxHealth = j["maxHealth"].get<float>();
            if (j.contains("currentHealth"))         comp.currentHealth = j["currentHealth"].get<float>();
            if (j.contains("invincibilityDuration")) comp.invincibilityDuration = j["invincibilityDuration"].get<float>();
            if (j.contains("team"))                  comp.team = static_cast<TeamID>(j["team"].get<int>());
        }
    };
} 

// ===================================================================
// 3. CombatRosterComponent
// ===================================================================
template <> struct ComponentMeta<CombatRosterComponent> {
    static constexpr const char* displayName    = "Combat Roster (部位名簿)";
    static constexpr const char* category       = CCL::Categories::Combat; // ★追加
    static constexpr int         sortPriority   = 175; // ★追加
    static constexpr bool        hasCustomGui   = true;
    static constexpr bool        isSerializable = true;

#ifdef ENABLE_EDITOR
    static bool CustomGui(CombatRosterComponent& comp, unsigned long long entityID, void* worldPtr) {
        bool changed = false;
        auto* world = static_cast<CCL::ECS::Core::World*>(worldPtr);
        if (!world) return false;

        std::vector<CCL::ECS::EntityID> childEntities;
        if (auto* trans = world->GetComponent<TransformComponent>(entityID)) {
            CCL::ECS::EntityID child = trans->firstChildID;
            while (child != 0) {
                childEntities.push_back(child);
                if (auto* childTrans = world->GetComponent<TransformComponent>(child)) {
                    child = childTrans->nextSiblingID;
                } else break;
            }
        }

        ImGui::Text("登録済みの部位/武器タグ:");
        ImGui::Separator();

        for (int i = 0; i < comp.count; ++i) {
            ImGui::PushID(i);
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 1.0f, 1.0f), "Slot %d", i);
            ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - 30.0f);
            if (ImGui::Button("X")) {
                for (int j = i; j < comp.count - 1; ++j) {
                    comp.entries[j] = comp.entries[j + 1];
                }
                comp.count--;
                changed = true;
                ImGui::PopID();
                break;
            }

            ImGui::Text(" Tag  "); ImGui::SameLine();
            ImGui::SetNextItemWidth(120.0f);

            const char* currentPreview = comp.entries[i].tag[0] != '\0' ? comp.entries[i].tag : "Select...";
            if (ImGui::BeginCombo("##TagCombo", currentPreview)) {
                const char* presets[] = { "RightHand", "LeftHand", "RightFoot", "LeftFoot", "Head", "Tail", "Weapon_A", "Weapon_B" };
                for (const char* p : presets) {
                    if (ImGui::Selectable(p, strcmp(comp.entries[i].tag, p) == 0)) {
                        strcpy_s(comp.entries[i].tag, sizeof(comp.entries[i].tag), p);
                        changed = true;
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(-1);
            if (ImGui::InputText("##TagInput", comp.entries[i].tag, sizeof(comp.entries[i].tag))) changed = true;

            ImGui::Text(" Bind "); ImGui::SameLine();
            ImGui::SetNextItemWidth(-1);

            char entityPreview[64];
            if (comp.entries[i].id == 0) sprintf_s(entityPreview, "None (Not Bound)");
            else sprintf_s(entityPreview, "Entity [%llu]", comp.entries[i].id);

            if (ImGui::BeginCombo("##EntityCombo", entityPreview)) {
                if (ImGui::Selectable("None", comp.entries[i].id == 0)) {
                    comp.entries[i].id = 0;
                    changed = true;
                }
                for (auto childID : childEntities) {
                    char label[64];
                    sprintf_s(label, "Child Entity [%llu]", childID);
                    if (ImGui::Selectable(label, comp.entries[i].id == childID)) {
                        comp.entries[i].id = childID;
                        changed = true;
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::Separator();
            ImGui::PopID();
        }

        ImGui::Spacing();
        if (comp.count < 4) {
            if (ImGui::Button("+ 新しい部位を追加", ImVec2(-1, 0))) {
                comp.entries[comp.count].id = 0;
                comp.entries[comp.count].tag[0] = '\0';
                comp.count++;
                changed = true;
            }
        } else {
            ImGui::TextDisabled("最大登録数(4)に達しています");
        }
        return changed;
    }
#else
    static bool CustomGui(CombatRosterComponent&, unsigned long long, void*) { return false; }
#endif

    static const std::vector<FieldDescriptor>& Fields() {
        static const std::vector<FieldDescriptor> empty;
        return empty;
    }
};
REGISTER_COMPONENT(CombatRosterComponent, "CombatRosterComponent")
REGISTER_COMPONENT_EDITOR_GUI(CombatRosterComponent)

// ===================================================================
// 4. DamageEventComponent (非保存タグ)
// ===================================================================
template <> struct ComponentMeta<DamageEventComponent> {
    static constexpr const char* displayName    = "Damage Event (一時データ)";
    static constexpr const char* category       = CCL::Categories::Internal; // ★追加
    static constexpr int         sortPriority   = 900; // ★追加
    static constexpr bool        hasCustomGui   = false;
    static constexpr bool        isSerializable = false;

    static const std::vector<FieldDescriptor>& Fields() {
        static const std::vector<FieldDescriptor> empty;
        return empty;
    }
};
REGISTER_COMPONENT(DamageEventComponent, "DamageEventComponent")
REGISTER_COMPONENT_EDITOR_GUI(DamageEventComponent)

// ===================================================================
// 5. HealthComponent
// ===================================================================
template <> struct ComponentMeta<HealthComponent> {
    static constexpr const char* displayName    = "Health (体力・陣営)";
    static constexpr const char* category       = CCL::Categories::Gameplay; // ★追加
    static constexpr int         sortPriority   = 100; // ★追加
    static constexpr bool        hasCustomGui   = true;
    static constexpr bool        isSerializable = true;

#ifdef ENABLE_EDITOR
    static bool CustomGui(HealthComponent& comp, unsigned long long /*entityID*/ = 0, void* /*worldPtr*/ = nullptr) {
        bool changed = false;

        const char* teamNames[] = { "Player (プレイヤー)", "Enemy (敵)", "Neutral (中立)" };
        int currentTeam = static_cast<int>(comp.team);

        ImGui::Text("Team / Faction:");
        ImGui::SetNextItemWidth(-1);
        if (ImGui::Combo("##TeamCombo", &currentTeam, teamNames, IM_ARRAYSIZE(teamNames))) {
            comp.team = static_cast<TeamID>(currentTeam);
            changed = true;
        }

        ImGui::Separator();
        ImGui::Text("Health Status:");

        float healthRatio = comp.maxHealth > 0.0f ? (comp.currentHealth / comp.maxHealth) : 0.0f;
        char overlay[32];
        sprintf_s(overlay, "%.1f / %.1f", comp.currentHealth, comp.maxHealth);
        ImGui::ProgressBar(healthRatio, ImVec2(-1.f, 0.f), overlay);

        if (ImGui::DragFloat("Max Health", &comp.maxHealth, 1.0f, 1.0f, 99999.0f)) {
            if (comp.currentHealth > comp.maxHealth) {
                comp.currentHealth = comp.maxHealth;
            }
            changed = true;
        }
        if (ImGui::DragFloat("Current Health", &comp.currentHealth, 1.0f, 0.0f, comp.maxHealth)) {
            changed = true;
        }

        ImGui::Separator();
        ImGui::Text("Invincibility (無敵設定):");
        if (ImGui::DragFloat("Duration (sec)", &comp.invincibilityDuration, 0.05f, 0.0f, 10.0f)) {
            changed = true;
        }

        if (comp.invincibilityTimer > 0.0f) {
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), ">> Invincible! (%.2f sec left) <<", comp.invincibilityTimer);
        } else {
            ImGui::TextDisabled("Not Invincible");
        }

        return changed;
    }
#else
    static bool CustomGui(HealthComponent&, unsigned long long, void*) { return false; }
#endif

    static const std::vector<FieldDescriptor>& Fields() {
        static const std::vector<FieldDescriptor> empty;
        return empty;
    }
};
REGISTER_COMPONENT(HealthComponent, "HealthComponent")
REGISTER_COMPONENT_EDITOR_GUI(HealthComponent)