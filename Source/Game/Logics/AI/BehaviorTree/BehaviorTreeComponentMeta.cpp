/**
 * @file BehaviorTreeComponentMeta.cpp
 * @brief AI関連コンポーネントのエディタ/シリアライズ用メタデータ定義
 */
#include "Game/Logics/AI/BehaviorTree/Data/BehaviorTreeComponents.h"
#include "Game/Logics/AI/BehaviorTree/Data/BehaviorTreeLoader.h"
#include "Game/Logics/AI/Drone/DroneComponent.h"
#include "Engine/Serialization/Registry/ComponentRegistry.h"
#include "Engine/Serialization/Meta/ComponentDefine.h"
#include "Engine/Serialization/Meta/ComponentCategories.h" // ★追加

#ifdef ENABLE_EDITOR
#include "Editor/Inspector/ComponentGuiRegistry.h"
#include "Engine/Serialization/Meta/ComponentMetaImGui.h"
#include "Engine/Platform/Core/Dialog.h"     
#include "Engine/Platform/Core/FileSystem.h" 
#include <imgui.h>
#endif

static const char *DroneFormationNames[] = {
    "Hidden", "OrbitCircle", "SequentialAttack", "DeathRing", "AegisShield",
    "HighOrbit", "LowOrbit", "SpreadLockOn", "AllCharge", "CloseGuard", "ChargeTunnel"};
static const char *DroneStateNames[] = {
    "Idle", "MoveToTarget", "LockOn", "FireCharge"};

// =======================================================================
// 1. BehaviorTreeComponent
// =======================================================================
template <> struct ComponentMeta<BehaviorTreeComponent> {
    CCL_META_USING_THIS(BehaviorTreeComponent)

    static constexpr const char *displayName    = "Behavior Tree";
    static constexpr const char *category       = CCL::Categories::AI; // ★追加
    static constexpr int         sortPriority   = 150; // ★追加
    static constexpr bool        hiddenInInspector = false;
    static constexpr bool        hasCustomGui   = true;
    static constexpr bool        isSerializable = true;

#ifdef ENABLE_EDITOR
    static bool CustomGui(BehaviorTreeComponent &comp, unsigned long long /*entityID*/ = 0, void * /*worldPtr*/ = nullptr) {
        bool changed = false;

        ImGui::TextDisabled("BTファイル: %s", comp.assetPath.empty() ? "None" : comp.assetPath.c_str());

        if (ImGui::Button("ビヘイビアツリーを読込... (JSON)", ImVec2(-1, 0))) {
            if (auto pathOpt = FileDialog::OpenPreset("BehaviorTree", "Select Behavior Tree", GetActiveWindow())) {
                comp.assetPath = FileDialog::ConvertToRelativePath(*pathOpt);
                if (!comp.sharedAsset) comp.sharedAsset = std::make_shared<BTAsset>();
                BehaviorTreeLoader::LoadFromJson(comp.assetPath, *comp.sharedAsset);
                comp.loadedAssetPath = comp.assetPath;
                changed = true;
            }
        }

        ImGui::Separator();
        if (ImGui::Button("AI再読込")) {
            if (!comp.sharedAsset) comp.sharedAsset = std::make_shared<BTAsset>();
            BehaviorTreeLoader::LoadFromJson(comp.assetPath, *comp.sharedAsset);
            changed = true;
        }

        ImGui::Separator();
        size_t nodeCount = comp.sharedAsset ? comp.sharedAsset->nodes.size() : 0;
        ImGui::LabelText("Nodes", "%zu", nodeCount);
        ImGui::LabelText("Current Node", "%u", comp.runningNodeId);

        return changed;
    }
#else
    static bool CustomGui(BehaviorTreeComponent &, unsigned long long, void *) { return false; }
#endif

    static const std::vector<FieldDescriptor> &Fields() {
        static const std::vector<FieldDescriptor> fields = {
            CFIELD(assetPath, "AIパス", "File")
            CFIELD(phase2AssetPath, "第2形態AIパス", "File")
            CFIELD_FLOAT_RANGED(phase2HealthThreshold, "第2形態移行HP閾値", 0.1f, 0.0f, 100.0f, "File")};
        return fields;
    }
};
REGISTER_COMPONENT(BehaviorTreeComponent, "BehaviorTreeComponent")
REGISTER_COMPONENT_EDITOR_GUI(BehaviorTreeComponent)

// =======================================================================
// 2. BossCommandComponent
// =======================================================================
template <> struct ComponentMeta<BossCommandComponent> {
    CCL_META_USING_THIS(BossCommandComponent)

    static constexpr const char* displayName    = "Boss Command (AI Output)";
    static constexpr const char *category       = CCL::Categories::AI; // ★追加
    static constexpr int         sortPriority   = 160; // ★追加
    static constexpr bool        hiddenInInspector = false;
    static constexpr bool        hasCustomGui   = false; 
    static constexpr bool        isSerializable = true;  

    static const std::vector<FieldDescriptor>& Fields() {
        static const std::vector<FieldDescriptor> fields = {
            CFIELD(currentActionId, "現在のActionID", "Current Command")
            CFIELD_ENUM_U8(requestFormation, "フォーメーション要求", DroneFormationNames, std::size(DroneFormationNames), "Drone Control")
            CFIELD(targetPlayerId, "ターゲット(Player)ID", "Drone Control")
        };
        return fields;
    }
};
REGISTER_COMPONENT(BossCommandComponent, "BossCommandComponent")
REGISTER_COMPONENT_EDITOR_GUI(BossCommandComponent)

// =======================================================================
// 3. DroneComponent
// =======================================================================
template <> struct ComponentMeta<DroneComponent> {
    CCL_META_USING_THIS(DroneComponent)

    static constexpr const char *displayName    = "Drone";
    static constexpr const char *category       = CCL::Categories::AI; // ★追加
    static constexpr int         sortPriority   = 170; // ★追加
    static constexpr bool        hiddenInInspector = false;
    static constexpr bool        hasCustomGui   = false;
    static constexpr bool        isSerializable = true;

    static const std::vector<FieldDescriptor> &Fields() {
        static const std::vector<FieldDescriptor> fields = {
            CFIELD(ownerBossId, "親ボスID", "Hierarchy") 
            CFIELD(localIndex, "ローカルインデックス", "Formation")
            CFIELD(totalDrones, "総ドローン数", "Formation")
            CFIELD_ENUM_U8(currentFormation, "現在の指示", DroneFormationNames, std::size(DroneFormationNames), "State")
            CFIELD_ENUM_U8(currentState, "現在ステート", DroneStateNames, std::size(DroneStateNames), "State")
            CFIELD(hasReachedSlot, "スロット到着済み", "State")
            CFIELD(stateTimer, "状態タイマー", "State")
            CFIELD_FLOAT3_FAST(targetPosition, "目標座標", 0.1f, "State")
            CFIELD_FLOAT_RANGED(moveSpeed, "移動速度", 0.1f, 0.0f, 100.0f, "Stats")
            CFIELD_FLOAT_RANGED(orbitRadius, "旋回半径", 0.1f, 0.0f, 100.0f, "Stats")};
        return fields;
    }
};
REGISTER_COMPONENT(DroneComponent, "DroneComponent")
REGISTER_COMPONENT_EDITOR_GUI(DroneComponent)