// 汎用AIコンポーネント（BehaviorTree / Drone）のエディタ/シリアライズ用メタデータ定義
//
// BossCommandComponent のメタは BossCommandComponentMeta.cpp へ移した。
// 構造体自体がボス側へ移ったので、メタだけここに残すと置き場がちぐはぐになる。
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
template <>
struct ComponentMeta<BehaviorTreeComponent>
{
    CCL_META_USING_THIS(BehaviorTreeComponent)

    static constexpr const char *displayName       = "Behavior Tree";
    static constexpr const char *category          = CCL::Categories::AI;
    static constexpr int         sortPriority      = 150;
    static constexpr bool        hiddenInInspector = false;
    static constexpr bool        hasCustomGui      = true;
    static constexpr bool        isSerializable    = true;

#ifdef ENABLE_EDITOR
    // sharedAsset はここで書かない。書き手は BehaviorTreeSystem::SyncAssets だけ。
    //   SyncAssets は「欲しい木 != 今の sharedAsset」で差し替えを検知し、その時だけ
    //   nodeTimers などを新しい木の長さで作り直す。ここで先に sharedAsset を差し替えると
    //   検知がすり抜け、前の木の長さのバッファが残って評価が毎フレーム見送られる。
    //   ここがやるのは「どの木を使うか（パス）」と「キャッシュを新しくする」ことだけ。
    static bool CustomGui(BehaviorTreeComponent &comp, unsigned long long /*entityID*/ = 0, void * /*worldPtr*/ = nullptr)
    {
        bool changed = false;

        ImGui::TextDisabled("BTファイル: %s", comp.assetPath.empty() ? "None" : comp.assetPath.c_str());

        if (ImGui::Button("ビヘイビアツリーを読込... (JSON)", ImVec2(-1, 0)))
        {
            if (auto pathOpt = FileDialog::OpenPreset("BehaviorTree", "Select Behavior Tree", GetActiveWindow()))
            {
                comp.assetPath = FileDialog::ConvertToRelativePath(*pathOpt);
                changed        = true;
            }
        }

        ImGui::Separator();
        // キャッシュを捨てて読み直す。キャッシュの中身が新しい木に替わるので、この個体も
        // 同じ木を使っている他の個体も、次のフレームの SyncAssets で受け取り直す。
        if (ImGui::Button("AI再読込"))
        {
            BTAssetCache::Instance().Reload(comp.assetPath);
            changed = true;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(読込済みの木: %zu 本)", BTAssetCache::Instance().Count());

        ImGui::Separator();
        // ノード数はキャッシュから引く。ゲームを止めて編集している間は SyncAssets が
        // 回らないので、sharedAsset を見ると選び直した木のノード数が出ない。
        // GetOrLoad は2回目以降は検索だけで、失敗したパスも読みに行かない。
        const auto pending   = BTAssetCache::Instance().GetOrLoad(comp.assetPath);
        size_t     nodeCount = pending ? pending->nodes.size() : 0;
        ImGui::LabelText("Nodes", "%zu", nodeCount);
        if (pending != comp.sharedAsset) ImGui::TextDisabled("（次のフレームで反映）");
        ImGui::LabelText("Current Node", "%u", comp.runningNodeId);

        return changed;
    }
#else
    static bool CustomGui(BehaviorTreeComponent &, unsigned long long, void *) { return false; }
#endif

    static const std::vector<FieldDescriptor> &Fields()
    {
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
template <>
struct ComponentMeta<DroneComponent>
{
    CCL_META_USING_THIS(DroneComponent)

    static constexpr const char *displayName       = "Drone";
    static constexpr const char *category          = CCL::Categories::AI;
    static constexpr int         sortPriority      = 170;
    static constexpr bool        hiddenInInspector = false;
    static constexpr bool        hasCustomGui      = false;
    static constexpr bool        isSerializable    = true;

    static const std::vector<FieldDescriptor> &Fields()
    {
        static const std::vector<FieldDescriptor> fields = {
            // 保存するのは「所属」と「個体パラメータ」だけ。
            //   下の _RT 群は書き手がシステムなので保存しない（CFIELD_RT の説明を参照）。
            //   ドローンは必ず DroneManagerSystem::SpawnDrones 経由で生まれ、
            //   localIndex / totalDrones はその中の ReindexAllDrones が振り直す。
            //   シーンに手置きされたドローンは存在しないので、保存しなくても復元できる。
            CFIELD(ownerBossId, "親ボスID", "Hierarchy")
                CFIELD_FLOAT_RANGED(moveSpeed, "移動速度", 0.1f, 0.0f, 100.0f, "Stats")
                    CFIELD_FLOAT_RANGED(orbitRadius, "旋回半径", 0.1f, 0.0f, 100.0f, "Stats")

                        CFIELD_RT(localIndex, "ローカルインデックス", "Formation")
                            CFIELD_RT(totalDrones, "総ドローン数", "Formation")
                                CFIELD_ENUM_U8_RT(currentFormation, "現在の指示", kDroneFormationNames.data(), kDroneFormationNames.size(), "State")
                                    CFIELD_ENUM_U8_RT(currentState, "現在ステート", DroneStateNames, std::size(DroneStateNames), "State")
                                        CFIELD_RT(hasReachedSlot, "スロット到着済み", "State")
                                            CFIELD_RT(stateTimer, "状態タイマー", "State")
                                                CFIELD_FLOAT3_RT(targetPosition, "目標座標", 0.1f, "State")};
        return fields;
    }
};
REGISTER_COMPONENT(DroneComponent, "DroneComponent")
REGISTER_COMPONENT_EDITOR_GUI(DroneComponent)
