#include "ECS/Core/CCL_World.h"
#include "Game/Logics/Combat/CombatComponents.h"
#include "Game/Logics/Combat/CombatRosterComponent.h"
#include "Engine/GamePlay/Transform/BoneAttachmentComponent.h"
#include "Editor/Inspector/ComponentGuiRegistry.h"
#include "Engine/Serialization/ComponentRegistry.h"
#include "Engine/Serialization/Meta/ComponentMeta.h"
#include "Engine/Serialization/Meta/ComponentMetaImGui.h"
#include "Engine/Serialization/Meta/ComponentMetaJson.h"

// ===================================================================
// 1. HitboxComponent (攻撃判定)
// ===================================================================
template <> struct ComponentMeta<HitboxComponent> {
    static constexpr bool registered = true;
    static constexpr const char* displayName = "Hitbox (攻撃判定)";
    static constexpr bool hasCustomGui = false;
    static constexpr bool isSerializable = true;

    static const std::vector<FieldDescriptor>& Fields() {
        static const std::vector<FieldDescriptor> fields = {
            META_FIELD_FLOAT(HitboxComponent, damageAmount, "基礎ダメージ量", "Damage", 1.0f, 0.0f, 9999.0f, "Combat"),
            META_FIELD_BOOL(HitboxComponent, isActive, "判定アクティブ(Debug)", "Is Active", "Combat")
            // ※ hitTargets や hitCount はランタイムの内部状態なのでインスペクタには露出させない
        };
        return fields;
    }
};
REGISTER_COMPONENT(HitboxComponent, "HitboxComponent");

// ===================================================================
// 2. HurtboxComponent (被弾判定)
// ===================================================================
template <> struct ComponentMeta<HurtboxComponent> {
    static constexpr bool registered = true;
    static constexpr const char* displayName = "Hurtbox (被弾判定)";
    static constexpr bool hasCustomGui = false;
    static constexpr bool isSerializable = true;

    static const std::vector<FieldDescriptor>& Fields() {
        static const std::vector<FieldDescriptor> fields = {
            META_FIELD_FLOAT(HurtboxComponent, damageMultiplier, "ダメージ倍率 (弱点等)", "Multiplier", 0.1f, 0.0f, 10.0f, "Combat")
        };
        return fields;
    }
};
REGISTER_COMPONENT(HurtboxComponent, "HurtboxComponent");

// ===================================================================
// 3. BoneAttachmentComponent (骨格追従)
// ===================================================================
template <> struct ComponentMeta<BoneAttachmentComponent> {
    static constexpr bool registered = true;
    static constexpr const char* displayName = "Bone Attachment (ボーン追従)";
    static constexpr bool hasCustomGui = false;
    static constexpr bool isSerializable = true;

    static const std::vector<FieldDescriptor>& Fields() {
        static const std::vector<FieldDescriptor> fields = {
            META_FIELD_STRING(BoneAttachmentComponent, boneName, "追従先ボーン名", "Target Bone Name", "Transform")
        };
        return fields;
    }
};
REGISTER_COMPONENT(BoneAttachmentComponent, "BoneAttachmentComponent");

// ===================================================================
// 4. CombatRosterComponent (武器・部位名簿)
// ===================================================================
// 固定長配列 (entries) を持つため、CustomGui を用いてエディタ上で直感的に編集できるようにする
template <> struct ComponentMeta<CombatRosterComponent> {
    static constexpr bool registered = true;
    static constexpr const char* displayName = "Combat Roster (部位名簿)";
    static constexpr bool hasCustomGui = true;
    static constexpr bool isSerializable = true; // セーブデータに保存する

    static bool CustomGui(CombatRosterComponent& comp, unsigned long long entityID, void* worldPtr) {
        bool changed = false;

        ImGui::Text("登録済みの部位/武器タグ:");
        ImGui::Separator();

        for (int i = 0; i < comp.count; ++i) {
            ImGui::PushID(i);
            ImGui::Text("Slot %d:", i);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(150.0f);

            // タグの文字列編集
            if (ImGui::InputText("##Tag", comp.entries[i].tag, sizeof(comp.entries[i].tag))) {
                changed = true;
            }

            // ※ EntityID は通常スクリプトや子エンティティ生成時に自動バインドされるため表示のみ
            ImGui::TextDisabled("  -> Bound EntityID: %llu", comp.entries[i].id);
            ImGui::PopID();
        }

        ImGui::Spacing();
        if (comp.count < 4) { // MAX=4
            if (ImGui::Button("+ 新しい部位を追加", ImVec2(-1, 0))) {
                comp.count++;
                changed = true;
            }
        }
        else {
            ImGui::TextDisabled("最大登録数(4)に達しています");
        }

        return changed;
    }

    static const std::vector<FieldDescriptor>& Fields() {
        static const std::vector<FieldDescriptor> fields = {};
        return fields;
    }
};
REGISTER_COMPONENT(CombatRosterComponent, "CombatRosterComponent");

// ===================================================================
// 5. DamageEventComponent (ダメージイベント)
// ===================================================================
// 【設計思想】システム間通信用の「揮発性手紙」なので、絶対にセーブしてはならない (isSerializable = false)
template <> struct ComponentMeta<DamageEventComponent> {
    static constexpr bool registered = true;
    static constexpr const char* displayName = "Damage Event (一時データ)";
    static constexpr bool hasCustomGui = false;
    static constexpr bool isSerializable = false; // ★保存しない

    static const std::vector<FieldDescriptor>& Fields() {
        static const std::vector<FieldDescriptor> fields = {};
        return fields;
    }
};
REGISTER_COMPONENT(DamageEventComponent, "DamageEventComponent");