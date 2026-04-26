/**
 * @file BehaviorTreeComponentMeta.cpp
 * @brief AI関連コンポーネントのエディタ/シリアライズ用メタデータ定義
 */
#include "Game/Logics/AI/BehaviorTree/Data/BehaviorTreeComponents.h"
#include "Game/Logics/AI/BehaviorTree/Data/BehaviorTreeLoader.h"
#include "Game/Logics/AI/BehaviorTree/Drone/DroneComponent.h"
#include "Editor/Inspector/ComponentGuiRegistry.h"
#include "Engine/Serialization/ComponentRegistry.h"
#include "Engine/Serialization/Meta/ComponentMeta.h"
#include "Engine/Serialization/Meta/ComponentMetaImGui.h"
#include "Engine/Serialization/Meta/ComponentMetaJson.h"
#include "Engine/Platform/Dialog.h"
#include <imgui.h>

 // =======================================================================
 // エディタ表示用のEnum文字列定義（プルダウンメニュー用）
 // =======================================================================
static const char* DroneFormationNames[] = { "Hidden", "OrbitCircle", "SequentialAttack" };
static const char* DroneStateNames[] = { "Idle", "MoveToTarget", "LockOn", "FireCharge" };


template <> struct ComponentMeta<BehaviorTreeComponent> {
    static constexpr bool registered = true;
    static constexpr const char* displayName = "Behavior Tree";
    static constexpr bool hasCustomGui = true; // カスタムGUIを有効化
    static constexpr bool isSerializable = true;

    // --- カスタムGUIでロード機能を統合 ---
    static bool CustomGui(BehaviorTreeComponent& comp, unsigned long long entityID, void* worldPtr) {
        bool changed = false;

        ImGui::Text("AI Logic Path:");
        char pathBuf[256];
        strcpy_s(pathBuf, comp.assetPath.c_str());

        if (ImGui::InputText("##Path", pathBuf, sizeof(pathBuf))) {
            comp.assetPath = pathBuf;
            changed = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("...")) {
            char path[MAX_PATH] = "";
            if (Dialog::OpenFileName(path, MAX_PATH, "BehaviorTree (*.json)\0*.json\0", "AIを選択") == DialogResult::OK) {
                comp.assetPath = path;
                // パスが変わったら即座にリロード！
                BehaviorTreeLoader::LoadFromJson(comp.assetPath, comp.runtimeAsset);
                changed = true;
            }
        }

        if (ImGui::Button("Reload AI")) {
            BehaviorTreeLoader::LoadFromJson(comp.assetPath, comp.runtimeAsset);
            changed = true;
        }

        ImGui::Separator();
        // 他のデバッグ情報表示
        ImGui::LabelText("Nodes", "%zu", comp.runtimeAsset.nodes.size());
        ImGui::LabelText("Current Node", "%u", comp.runningNodeId);

        return changed;
    }

    static const std::vector<FieldDescriptor>& Fields() {
        static const std::vector<FieldDescriptor> fields = {
            { "AIパス", "assetPath", FieldKind::String, offsetof(BehaviorTreeComponent, assetPath), 0, 0, 0, "File", nullptr, 0, true }
        };
        return fields;
    }
};
REGISTER_COMPONENT(BehaviorTreeComponent, "BehaviorTreeComponent")


// --- BossCommandComponent（保存しない） ---
template <> struct ComponentMeta<BossCommandComponent> {
    static constexpr bool registered = true;
    static constexpr const char* displayName = "Boss Command (AI Output)";
    static constexpr bool hasCustomGui = false;
    static constexpr bool isSerializable = false;

    static const std::vector<FieldDescriptor>& Fields() {
        static const std::vector<FieldDescriptor> fields = {
            { "近接攻撃要求", "requestMeleeAttack", FieldKind::Bool, offsetof(BossCommandComponent, requestMeleeAttack), 1.0f, 0.0f, 0.0f, "Melee", nullptr, 0, false },
            { "突進要求", "requestCharge", FieldKind::Bool, offsetof(BossCommandComponent, requestCharge), 1.0f, 0.0f, 0.0f, "Melee", nullptr, 0, false },
            { "移動要求", "requestMove", FieldKind::Bool, offsetof(BossCommandComponent, requestMove), 1.0f, 0.0f, 0.0f, "Movement", nullptr, 0, false },
            { "移動目標座標", "moveTarget", FieldKind::Float3, offsetof(BossCommandComponent, moveTarget), 0.1f, 0.0f, 0.0f, "Movement", nullptr, 0, false },

            // ★追加: ドローンに対するフォーメーション要求とターゲットID
            { "フォーメーション要求", "requestFormation", FieldKind::EnumU8, offsetof(BossCommandComponent, requestFormation), 1.0f, 0.0f, 0.0f, "Drone Control", DroneFormationNames, 3, false },
            { "ターゲット(Player)ID", "targetPlayerId", FieldKind::EntityID, offsetof(BossCommandComponent, targetPlayerId), 1.0f, 0.0f, 0.0f, "Drone Control", nullptr, 0, false }
        };
        return fields;
    }
};
REGISTER_COMPONENT(BossCommandComponent, "BossCommandComponent")


// --- DroneComponent（保存する） ---
template <> struct ComponentMeta<DroneComponent> {
    static constexpr bool registered = true;
    static constexpr const char* displayName = "Drone (ドローン)";
    static constexpr bool hasCustomGui = false;
    static constexpr bool isSerializable = true;

    static const std::vector<FieldDescriptor>& Fields() {
        static const std::vector<FieldDescriptor> fields = {
            // ★修正: EntityID に対応したためコメントアウトを解除
            { "親ボスID", "ownerBossId", FieldKind::EntityID, offsetof(DroneComponent, ownerBossId), 1.0f, 0.0f, 0.0f, "Hierarchy", nullptr, 0, true },
            { "ローカルインデックス", "localIndex", FieldKind::UInt16, offsetof(DroneComponent, localIndex), 1.0f, 0.0f, 0.0f, "Formation", nullptr, 0, true },
            { "総ドローン数", "totalDrones", FieldKind::UInt16, offsetof(DroneComponent, totalDrones), 1.0f, 0.0f, 0.0f, "Formation", nullptr, 0, true },

            // ★追加: 現在のフォーメーションとステートをプルダウン表示
            { "現在の指示", "currentFormation", FieldKind::EnumU8, offsetof(DroneComponent, currentFormation), 1.0f, 0.0f, 0.0f, "State", DroneFormationNames, 3, false },
            { "現在ステート", "currentState", FieldKind::EnumU8, offsetof(DroneComponent, currentState), 1.0f, 0.0f, 0.0f, "State", DroneStateNames, 4, false },
            { "状態タイマー", "stateTimer", FieldKind::Float, offsetof(DroneComponent, stateTimer), 0.1f, 0.0f, 0.0f, "State", nullptr, 0, false },

            { "目標座標", "targetPosition", FieldKind::Float3, offsetof(DroneComponent, targetPosition), 0.1f, 0.0f, 0.0f, "State", nullptr, 0, false },
            { "移動速度", "moveSpeed", FieldKind::Float, offsetof(DroneComponent, moveSpeed), 0.1f, 0.0f, 100.0f, "Stats", nullptr, 0, true },
            { "旋回半径", "orbitRadius", FieldKind::Float, offsetof(DroneComponent, orbitRadius), 0.1f, 0.0f, 100.0f, "Stats", nullptr, 0, true }
        };
        return fields;
    }
};
REGISTER_COMPONENT(DroneComponent, "DroneComponent")