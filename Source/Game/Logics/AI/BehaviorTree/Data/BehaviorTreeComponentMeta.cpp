/**
 * @file BehaviorTreeComponentMeta.cpp
 * @brief AI関連コンポーネントのエディタ/シリアライズ用メタデータ定義
 */
#include "Game/Logics/AI/BehaviorTree/Data/BehaviorTreeComponents.h"
#include "Game/Logics/AI/BehaviorTree/Data/BehaviorTreeLoader.h"
#include "Game/Logics/AI/Drone/DroneComponent.h"
#include <array>
#include "Engine/Serialization/Registry/ComponentRegistry.h"
#include "Engine/Serialization/Meta/ComponentDefine.h"
#include "Engine/Serialization/Meta/ComponentCategories.h"

#ifdef ENABLE_EDITOR
#include "Editor/Inspector/ComponentGuiRegistry.h"
#include "Engine/Serialization/Meta/ComponentMetaImGui.h"
#include "Engine/Platform/Core/Dialog.h"     
#include "Engine/Platform/Core/FileSystem.h" 
#include <imgui.h>
#endif

/// @brief インスペクタの列挙表示用の陣形名。
///
/// なぜ欠番ぶんも含む長さで作るのか:
///   列挙表示は「保存値をそのまま配列の添字にする」実装なので、実装済みだけを
///   詰めた配列にすると、値5(HighOrbit)が3番目の名前で表示される。
///
/// なぜ手書きの配列をやめたのか:
///   以前は33個の名前を並び順で対応させていた。enum を1行消すだけで
///   以降の全部が1つずれ、しかもコンパイルは通る。DRONE_FORMATION_LIST から
///   生成すれば、名前と値の対応を人間が保つ必要がなくなる。
static constexpr std::array<const char *, kDroneFormationSlotCount> MakeDroneFormationNames()
{
    std::array<const char *, kDroneFormationSlotCount> names{};
    for (auto &n : names) n = "(欠番)";
#define CCL_DRONE_FORMATION_NAME(Name, Value, Display) names[Value] = Display;
    DRONE_FORMATION_LIST(CCL_DRONE_FORMATION_NAME)
#undef CCL_DRONE_FORMATION_NAME
    return names;
}
static constexpr auto kDroneFormationNames = MakeDroneFormationNames();

static const char *DroneStateNames[] = {
    "Idle", "MoveToTarget", "FireCharge", "Stunned", "Returning"};

// =======================================================================
// 1. BehaviorTreeComponent
// =======================================================================
template <> struct ComponentMeta<BehaviorTreeComponent> {
    CCL_META_USING_THIS(BehaviorTreeComponent)

    static constexpr const char *displayName    = "Behavior Tree";
    static constexpr const char *category       = CCL::Categories::AI;
    static constexpr int         sortPriority   = 150;
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
    static constexpr const char *category       = CCL::Categories::AI;
    static constexpr int         sortPriority   = 160;
    static constexpr bool        hiddenInInspector = false;
    static constexpr bool        hasCustomGui   = false; 
    static constexpr bool        isSerializable = true;  

    static const std::vector<FieldDescriptor>& Fields() {
        static const std::vector<FieldDescriptor> fields = {
            CFIELD(currentActionId, "現在のActionID", "Current Command")
            CFIELD_ENUM_U8(requestFormation, "フォーメーション要求", kDroneFormationNames.data(), kDroneFormationNames.size(), "Drone Control")
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
    static constexpr const char *category       = CCL::Categories::AI;
    static constexpr int         sortPriority   = 170;
    static constexpr bool        hiddenInInspector = false;
    static constexpr bool        hasCustomGui   = false;
    static constexpr bool        isSerializable = true;

    static const std::vector<FieldDescriptor> &Fields() {
        static const std::vector<FieldDescriptor> fields = {
            CFIELD(ownerBossId, "親ボスID", "Hierarchy") 
            CFIELD(localIndex, "ローカルインデックス", "Formation")
            CFIELD(totalDrones, "総ドローン数", "Formation")
            CFIELD_ENUM_U8(currentFormation, "現在の指示", kDroneFormationNames.data(), kDroneFormationNames.size(), "State")
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