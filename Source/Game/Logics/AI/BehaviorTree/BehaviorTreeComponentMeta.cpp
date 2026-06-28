/**
 * @file BehaviorTreeComponentMeta.cpp
 * @brief AI関連コンポーネントのエディタ/シリアライズ用メタデータ定義
 */
#include "Game/Logics/AI/BehaviorTree/Data/BehaviorTreeComponents.h"
#include "Game/Logics/AI/BehaviorTree/Data/BehaviorTreeLoader.h"
#include "Game/Logics/AI/BehaviorTree/Drone/DroneComponent.h"
#include "Engine/Serialization/Registry/ComponentRegistry.h"

// ★ マニュアル絶対ルール: 必須ヘッダーに変更
#include "Engine/Serialization/Meta/ComponentDefine.h"

#ifdef ENABLE_EDITOR
#include "Editor/Inspector/ComponentGuiRegistry.h"
#include "Engine/Serialization/Meta/ComponentMetaImGui.h"
#include "Engine/Platform/Core/Dialog.h"     // FileDialog用
#include "Engine/Platform/Core/FileSystem.h" // FileSystem用
#include <imgui.h>
#endif

// =======================================================================
// エディタ表示用のEnum文字列定義（プルダウンメニュー用）
// =======================================================================
static const char *DroneFormationNames[] = {
    "Hidden", "OrbitCircle", "SequentialAttack", "DeathRing", "AegisShield",
    "HighOrbit", "LowOrbit", "SpreadLockOn", "AllCharge", "CloseGuard", "ChargeTunnel"};
static const char *DroneStateNames[] = {
    "Idle", "MoveToTarget", "LockOn", "FireCharge"};

// =======================================================================
// 1. BehaviorTreeComponent
// =======================================================================
template <>
struct ComponentMeta<BehaviorTreeComponent>
{
    CCL_META_USING_THIS(BehaviorTreeComponent) // ★ CFIELDを使うための必須マクロ

    static constexpr const char *displayName    = "Behavior Tree";
    static constexpr bool        hasCustomGui   = true;
    static constexpr bool        isSerializable = true;

#ifdef ENABLE_EDITOR
    static bool CustomGui(BehaviorTreeComponent &comp, unsigned long long /*entityID*/ = 0, void * /*worldPtr*/ = nullptr)
    {
        bool changed = false;

        ImGui::TextDisabled("BT File: %s", comp.assetPath.empty() ? "None" : comp.assetPath.c_str());

        // ★ マニュアル準拠: Engine::FileDialog を使用
        if (ImGui::Button("Load Behavior Tree... (JSON)", ImVec2(-1, 0)))
        {
            // 第1引数にコンテキスト、第2引数にタイトル、第3引数にウィンドウハンドル
            if (auto pathOpt = FileDialog::OpenPreset("BehaviorTree", "Select Behavior Tree", GetActiveWindow()))
            {

                // 1. 絶対パスからプロジェクト相対パスに自動変換 (チーム開発でのパス割れ防止)
                comp.assetPath = FileDialog::ConvertToRelativePath(*pathOpt);

                // 2. メモリバッファ確保
                if (!comp.sharedAsset)
                {
                    comp.sharedAsset = std::make_shared<BTAsset>();
                }

                // 3. 即座にロードしてバインド
                BehaviorTreeLoader::LoadFromJson(comp.assetPath, *comp.sharedAsset);

                // 4. 重複ロード防止フラグの更新
                comp.loadedAssetPath = comp.assetPath;

                changed = true;
            }
        }

        ImGui::Separator();

        if (ImGui::Button("Reload AI"))
        {
            if (!comp.sharedAsset)
                comp.sharedAsset = std::make_shared<BTAsset>();
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
    static bool CustomGui(BehaviorTreeComponent &, unsigned long long, void *)
    {
        return false;
    }
#endif

    // ★ マニュアル準拠: 生の FieldDescriptor 配列ではなく CFIELD マクロを使用
    static const std::vector<FieldDescriptor> &Fields()
    {
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
// 2. BossCommandComponent（空枠保存 ＋ インスペクタ表示）
// =======================================================================
template <> struct ComponentMeta<BossCommandComponent> {
    CCL_META_USING_THIS(BossCommandComponent)

    static constexpr const char* displayName = "Boss Command (AI Output)";
    static constexpr bool hasCustomGui = false; // 自動描画システムに委譲
    static constexpr bool isSerializable = true;  // ★ true に変更（これでアタッチ状態がシーンに保存されます）

    // シリアライズ処理は上記の ComponentSerializer が身代わりになって空化するため、
    // ここに Fields() を残しておいても中身が JSON に書き出されることはありません。
    // 純粋に「インスペクタ画面で現在の値をモニタリングする」ためだけに機能します。
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
// 3. DroneComponent（保存する）
// =======================================================================
template <>
struct ComponentMeta<DroneComponent>
{
    CCL_META_USING_THIS(DroneComponent)

    static constexpr const char *displayName    = "Drone (ドローン)";
    static constexpr bool        hasCustomGui   = false;
    static constexpr bool        isSerializable = true;

    static const std::vector<FieldDescriptor> &Fields()
    {
        static const std::vector<FieldDescriptor> fields = {
            CFIELD(ownerBossId, "親ボスID", "Hierarchy") // ★コンポーネント側は EntityLink であること！
            CFIELD(localIndex, "ローカルインデックス", "Formation")
            CFIELD(totalDrones, "総ドローン数", "Formation")
            CFIELD_ENUM_U8(currentFormation, "現在の指示", DroneFormationNames, std::size(DroneFormationNames), "State")
            CFIELD_ENUM_U8(currentState, "現在ステート", DroneStateNames, std::size(DroneStateNames), "State")
            CFIELD(stateTimer, "状態タイマー", "State")
            CFIELD_FLOAT3_FAST(targetPosition, "目標座標", 0.1f, "State")
            CFIELD_FLOAT_RANGED(moveSpeed, "移動速度", 0.1f, 0.0f, 100.0f, "Stats")
            CFIELD_FLOAT_RANGED(orbitRadius, "旋回半径", 0.1f, 0.0f, 100.0f, "Stats")};
        return fields;
    }
};
REGISTER_COMPONENT(DroneComponent, "DroneComponent")
REGISTER_COMPONENT_EDITOR_GUI(DroneComponent)