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
            { "アセットID", "assetId", FieldKind::UInt32, offsetof(BehaviorTreeComponent, assetId), 1.0f, 0.0f, 0.0f, "AI", nullptr, 0, false },
            { "実行中ノード", "runningNodeId", FieldKind::UInt16, offsetof(BehaviorTreeComponent, runningNodeId), 1.0f, 0.0f, 0.0f, "AI", nullptr, 0, false },
            { "ターゲット距離", "distanceToTarget", FieldKind::Float, offsetof(BehaviorTreeComponent, distanceToTarget), 0.1f, 0.0f, 0.0f, "Blackboard", nullptr, 0, false },
            { "HP(%)", "healthPercentage", FieldKind::Float, offsetof(BehaviorTreeComponent, healthPercentage), 0.1f, 0.0f, 100.0f, "Blackboard", nullptr, 0, false }
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
    static constexpr bool isSerializable = false; // 毎フレーム上書きされるため保存不要

    static const std::vector<FieldDescriptor>& Fields() {
        static const std::vector<FieldDescriptor> fields = {
            { "近接攻撃要求", "requestMeleeAttack", FieldKind::Bool, offsetof(BossCommandComponent, requestMeleeAttack), 1.0f, 0.0f, 0.0f, "Command", nullptr, 0, false },
            { "突進要求", "requestCharge", FieldKind::Bool, offsetof(BossCommandComponent, requestCharge), 1.0f, 0.0f, 0.0f, "Command", nullptr, 0, false }
        };
        return fields;
    }
};
REGISTER_COMPONENT(BossCommandComponent, "BossCommandComponent")