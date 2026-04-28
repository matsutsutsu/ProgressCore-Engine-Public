#include "ECS/Core/CCL_World.h"
#include "Game/Logics/Combat/CombatComponents.h"
#include "Game/Logics/Combat/CombatRosterComponent.h"
#include "Engine/GamePlay/Transform/BoneAttachmentComponent.h"
#include "Editor/Inspector/ComponentGuiRegistry.h"
#include "Engine/Serialization/ComponentRegistry.h"
#include "Engine/Serialization/Meta/ComponentMeta.h"
#include "Engine/Serialization/Meta/ComponentMetaImGui.h"
#include "Engine/Serialization/Meta/ComponentMetaJson.h"

#include "Engine/GamePlay/Transform/TransformComponent.h"
#include "Engine/GamePlay/Transform/PendingParentComponent.h"
#include "Engine/GamePlay/Graphics/Core/ModelComponent.h"
#include "Engine/Graphics/Resource/Model.h"

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
    static constexpr bool hasCustomGui = true; // ★自動生成GUIを切り、自前で描画する
    static constexpr bool isSerializable = true;

    static bool CustomGui(BoneAttachmentComponent& comp, unsigned long long entityID, void* worldPtr) {
        bool changed = false;
        auto* world = static_cast<CCL::ECS::Core::World*>(worldPtr);
        if (!world) return false;

        // 1. 親エンティティのIDを探す
        // （既に親子関係が解決済みなら Transform に、生成直後なら PendingParent に入っている）
        CCL::ECS::EntityID parentID = 0;
        if (auto* trans = world->GetComponent<TransformComponent>(entityID)) {
            parentID = trans->parentID;
        }
        if (parentID == 0) {
            if (auto* pending = world->GetComponent<PendingParentComponent>(entityID)) {
                parentID = pending->parentID;
            }
        }

        ImGui::Text("Target Bone Name:");
        ImGui::SetNextItemWidth(-1);

        // 2. 親が見つかった場合、ボーンリストを抽出してプルダウンを描画する
        if (parentID != 0) {
            if (auto* modelComp = world->GetComponent<ModelComponent>(parentID)) {
                if (Model* model = modelComp->GetModel()) {
                    const auto& nodes = model->GetNodes();
                    if (!nodes.empty()) {

                        // 現在の選択状態を表示
                        const char* currentPreview = comp.boneName[0] != '\0' ? comp.boneName : "Select Bone...";

                        if (ImGui::BeginCombo("##BoneCombo", currentPreview)) {
                            // 「追従を解除する」ための None オプション
                            if (ImGui::Selectable("None", comp.boneName[0] == '\0')) {
                                comp.boneName[0] = '\0';
                                comp.cachedBoneIndex = -1; // キャッシュをリセット
                                changed = true;
                            }

                            // 取得したボーン(ノード)を全てリストアップ
                            for (int i = 0; i < nodes.size(); ++i) {
                                bool isSelected = (strcmp(comp.boneName, nodes[i].name.c_str()) == 0);
                                if (ImGui::Selectable(nodes[i].name.c_str(), isSelected)) {
                                    strcpy_s(comp.boneName, sizeof(comp.boneName), nodes[i].name.c_str());
                                    comp.cachedBoneIndex = -1; // 名前が変わったら検索キャッシュを必ず破棄する
                                    changed = true;
                                }
                                if (isSelected) ImGui::SetItemDefaultFocus();
                            }
                            ImGui::EndCombo();
                        }
                        return changed; // プルダウンを描画できたらここで終了
                    }
                }
            }
        }

        // 3. 例外処理（親がいない、または親がモデルを持っていない場合の手打ちフォールバック）
        ImGui::TextColored(ImVec4(1, 1, 0, 1), "[Warning] Parent Model not found.");
        if (ImGui::InputText("##BoneNameInput", comp.boneName, sizeof(comp.boneName))) {
            comp.cachedBoneIndex = -1; // 手打ちで変更された場合もキャッシュを破棄
            changed = true;
        }

        return changed;
    }

    static const std::vector<FieldDescriptor>& Fields() {
        static const std::vector<FieldDescriptor> fields = {}; // CustomGui で処理するため空でOK
        return fields;
    }
};
REGISTER_COMPONENT(BoneAttachmentComponent, "BoneAttachmentComponent");

// ===================================================================
// 4. CombatRosterComponent (武器・部位名簿)
// ===================================================================
template <> struct ComponentMeta<CombatRosterComponent> {
    static constexpr bool registered = true;
    static constexpr const char* displayName = "Combat Roster (部位名簿)";
    static constexpr bool hasCustomGui = true;
    static constexpr bool isSerializable = true;

    static bool CustomGui(CombatRosterComponent& comp, unsigned long long entityID, void* worldPtr) {
        bool changed = false;
        auto* world = static_cast<CCL::ECS::Core::World*>(worldPtr);
        if (!world) return false;

        // ------------------------------------------------------------------
        // [準備] 親エンティティ（ボス）の「子エンティティ」のリストを収集する
        // ------------------------------------------------------------------
        std::vector<CCL::ECS::EntityID> childEntities;
        if (auto* trans = world->GetComponent<TransformComponent>(entityID)) {
            CCL::ECS::EntityID child = trans->firstChildID;
            while (child != 0) {
                childEntities.push_back(child);
                if (auto* childTrans = world->GetComponent<TransformComponent>(child)) {
                    child = childTrans->nextSiblingID;
                }
                else break;
            }
        }

        ImGui::Text("登録済みの部位/武器タグ:");
        ImGui::Separator();

        for (int i = 0; i < comp.count; ++i) {
            ImGui::PushID(i);

            // ヘッダーと削除ボタン
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 1.0f, 1.0f), "Slot %d", i);
            ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - 30.0f);
            if (ImGui::Button("X")) {
                // 配列を詰めて削除する処理
                for (int j = i; j < comp.count - 1; ++j) {
                    comp.entries[j] = comp.entries[j + 1];
                }
                comp.count--;
                changed = true;
                ImGui::PopID();
                break; // 削除したフレームはループを抜けて次回再描画
            }

            // --------------------------------------------------------------
            // ① Tagの入力 (プリセットからの選択 ＋ 自由入力)
            // --------------------------------------------------------------
            ImGui::Text(" Tag  "); ImGui::SameLine();
            ImGui::SetNextItemWidth(120.0f);

            const char* currentPreview = comp.entries[i].tag[0] != '\0' ? comp.entries[i].tag : "Select...";
            if (ImGui::BeginCombo("##TagCombo", currentPreview)) {
                // AAAゲームでよく使われる汎用タグのプリセット
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
            ImGui::SetNextItemWidth(-1); // 残りの幅を手打ち用に
            if (ImGui::InputText("##TagInput", comp.entries[i].tag, sizeof(comp.entries[i].tag))) {
                changed = true; // プルダウンにない特殊な名前の手打ちも許可する
            }

            // --------------------------------------------------------------
            // ② 紐付ける Entity の選択 (子エンティティリストから選ぶ)
            // --------------------------------------------------------------
            ImGui::Text(" Bind "); ImGui::SameLine();
            ImGui::SetNextItemWidth(-1);

            char entityPreview[64];
            if (comp.entries[i].id == 0) sprintf_s(entityPreview, "None (Not Bound)");
            else sprintf_s(entityPreview, "Entity [%llu]", comp.entries[i].id);

            if (ImGui::BeginCombo("##EntityCombo", entityPreview)) {
                // バインド解除用
                if (ImGui::Selectable("None", comp.entries[i].id == 0)) {
                    comp.entries[i].id = 0;
                    changed = true;
                }
                // 収集した子エンティティをリスト表示
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