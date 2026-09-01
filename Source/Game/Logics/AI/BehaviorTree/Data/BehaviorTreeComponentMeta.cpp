// 汎用AIコンポーネント（BehaviorTree / Drone）のエディタ/シリアライズ用メタデータ定義
//
// @note BossCommandComponent のメタは BossCommandComponentMeta.cpp へ移した。
//       構造体自体がボス側へ移ったので、メタだけここに残すと置き場がちぐはぐになる。
#include "Game/Logics/AI/BehaviorTree/Data/BehaviorTreeComponents.h"
#include "Game/Logics/AI/BehaviorTree/Data/BTAssetCache.h"
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

// 陣形名のテーブル（kDroneFormationNames）は DroneComponent.h にある。
// 列挙の定義と同じ場所に置くことで、名前と値の対応を人間が保つ必要をなくしている。

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

                // その場で1回取りに行く（BehaviorTreeSystem も次のフレームに同じことをするが、
                // ゲームを止めて編集しているときにノード数が出ないと不便なため）。
                comp.sharedAsset     = BTAssetCache::Instance().GetOrLoad(comp.assetPath);
                comp.loadedAssetPath = comp.sharedAsset ? comp.assetPath : std::string();
                changed = true;
            }
        }

        ImGui::Separator();
        // キャッシュを捨てて読み直す。同じ木を使っている他のエンティティも、
        // 次のフレームの BehaviorTreeSystem::SyncAssets で新しい木を受け取り直す。
        if (ImGui::Button("AI再読込")) {
            comp.sharedAsset     = BTAssetCache::Instance().Reload(comp.assetPath);
            comp.loadedAssetPath = comp.sharedAsset ? comp.assetPath : std::string();
            changed = true;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(読込済みの木: %zu 本)", BTAssetCache::Instance().Count());

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
        // 第2形態のパスとHP閾値は BossPhaseComponent へ移動済み
        // （どの敵でも持つ汎用データではないため）。
        static const std::vector<FieldDescriptor> fields = {
            CFIELD(assetPath, "AIパス", "File")};
        return fields;
    }
};
REGISTER_COMPONENT(BehaviorTreeComponent, "BehaviorTreeComponent")
REGISTER_COMPONENT_EDITOR_GUI(BehaviorTreeComponent)

// =======================================================================
// 2. DroneComponent
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
