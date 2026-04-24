/**
 * @file BehaviorTreeComponentMeta.cpp
 * @brief AI関連コンポーネントのエディタ/シリアライズ用メタデータ定義
 */
#include "Game/Logics/AI/BehaviorTree/BehaviorTreeComponents.h"
#include "Editor/Inspector/ComponentGuiRegistry.h"
#include "Engine/Serialization/ComponentRegistry.h"
#include "Engine/Serialization/Meta/ComponentMeta.h"
#include "Engine/Serialization/Meta/ComponentMetaImGui.h"
#include "Engine/Serialization/Meta/ComponentMetaJson.h"
#include <imgui.h>

 // --- BehaviorTreeComponent（保存する） ---
template <> struct ComponentMeta<BehaviorTreeComponent> {
    static constexpr bool registered = true;
    static constexpr const char* displayName = "Behavior Tree";
    static constexpr bool hasCustomGui = false;
    static constexpr bool isSerializable = true; // セーブデータに含める

    static const std::vector<FieldDescriptor>& Fields() {
        static const std::vector<FieldDescriptor> fields = {
            // ★修正: 末尾の serialize フラグをすべて true に変更し、JSON保存の対象にする
            { "アセットID", "assetId", FieldKind::UInt32, offsetof(BehaviorTreeComponent, assetId), 1.0f, 0.0f, 0.0f, "AI", nullptr, 0, true },
            { "実行中ノード", "runningNodeId", FieldKind::UInt16, offsetof(BehaviorTreeComponent, runningNodeId), 1.0f, 0.0f, 0.0f, "AI", nullptr, 0, true },

            // Blackboard のデータもセーブ可能に
            { "ターゲット距離", "distanceToTarget", FieldKind::Float, offsetof(BehaviorTreeComponent, blackboard.distanceToTarget), 0.1f, 0.0f, 0.0f, "Blackboard", nullptr, 0, true },
            { "HP(%)", "healthPercentage", FieldKind::Float, offsetof(BehaviorTreeComponent, blackboard.healthPercentage), 0.1f, 0.0f, 100.0f, "Blackboard", nullptr, 0, true }
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
    static constexpr bool isSerializable = false; // 毎フレーム上書きされるためコンポーネント自体を保存不要とする

    static const std::vector<FieldDescriptor>& Fields() {
        static const std::vector<FieldDescriptor> fields = {
            // ★修正: AIが出力するすべての変数をエディタ（ImGui）で監視できるように追加
            // ※このコンポーネントは isSerializable = false なので末尾は false のままで良い
            { "近接攻撃要求", "requestMeleeAttack", FieldKind::Bool, offsetof(BossCommandComponent, requestMeleeAttack), 1.0f, 0.0f, 0.0f, "Command", nullptr, 0, false },
            { "突進要求", "requestCharge", FieldKind::Bool, offsetof(BossCommandComponent, requestCharge), 1.0f, 0.0f, 0.0f, "Command", nullptr, 0, false },
            { "ガード要求", "requestGuard", FieldKind::Bool, offsetof(BossCommandComponent, requestGuard), 1.0f, 0.0f, 0.0f, "Command", nullptr, 0, false },
            { "移動要求", "requestMove", FieldKind::Bool, offsetof(BossCommandComponent, requestMove), 1.0f, 0.0f, 0.0f, "Command", nullptr, 0, false },
            { "移動目標座標", "moveTarget", FieldKind::Float3, offsetof(BossCommandComponent, moveTarget), 0.1f, 0.0f, 0.0f, "Command", nullptr, 0, false }
        };
        return fields;
    }
};
REGISTER_COMPONENT(BossCommandComponent, "BossCommandComponent")