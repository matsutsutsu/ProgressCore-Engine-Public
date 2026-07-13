/// @file  CommanderAI.h
/// @brief ボス司令官の StateTree。戦術状態を選び、各状態が使う BehaviorTree を
///        切り替える（StateTree → BT ブリッジ）。
///
/// 3層構造のうち「StateTree（戦術決定）」の層:
///   - Control / Pressure / Aggressive / Recover / Final / Broken の戦術状態
///   - 各状態のタスク = SetActiveBTTask（使う BT のパスを書き換えるだけ）
///   - 実際の攻撃実行は BossAISystem（既存の BT 評価）が担う
///
/// ブリッジの仕組み:
///   SetActiveBTTask が BehaviorTreeComponent.assetPath を書き換える
///     → BossAISystem の既存ロード機構が新しい BT を自動ロード
///     → その BT が currentActionId を出力
///     → BossActionSystem が実行
#pragma once
#include "Game/Logics/AI/StateTree/Core/StateTreeAsset.h"
#include "Game/Logics/AI/StateTree/Core/StateTreeNode.h"
#include "Game/Logics/AI/StateTree/Core/NodeRegistry.h"
#include "Game/Logics/AI/StateTree/Core/NodeParamField.h"
#include "Game/Logics/AI/Battle/BattleContext.h"

#include "Game/Logics/AI/BehaviorTree/Data/BehaviorTreeComponents.h" // BehaviorTreeComponent
#include "Game/Logics/Combat/HealthComponent.h"

#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/Platform/Core/Logger.h"

#include <cstring>

namespace CommanderAI
{
// ====================================================================
// ★ブリッジタスク★ 使う BehaviorTree を切り替える
// ====================================================================
/// @brief StateTree の状態に滞在している間、その戦術用の BT を有効化する。
///        assetPath を書き換えるだけで、既存の BossAISystem がロードする。
struct SetActiveBTTask : public CCL::AI::ITask
{
    struct Params
    {
        char btPath[128] = ""; // 例: "Data/BehaviorTree/Pressure.json"
    };

    static const std::vector<CCL::AI::NodeParamField> &ParamFields()
    {
        static const std::vector<CCL::AI::NodeParamField> f = {
            NODE_PARAM_STRING(Params, btPath, "BT Path", ""),
        };
        return f;
    }

    CCL::AI::RunStatus Tick(CCL::AI::ExecContext &ctx) override
    {
        const auto *p      = static_cast<const Params *>(ctx.params);
        auto       *btComp = ctx.world->GetComponent<BehaviorTreeComponent>(ctx.entity);
        if (!btComp) return CCL::AI::RunStatus::Failed;

        // 戦術が変わった = 使う BT を切り替える
        // （assetPath != loadedAssetPath になると BossAISystem が自動ロード）
        if (btComp->assetPath != p->btPath && p->btPath[0] != '\0')
        {
            btComp->assetPath = p->btPath;
            CCL_LOG_INFO(LogCategory::AI,
                "[Commander] 戦術BT切替 → %s", p->btPath);
        }
        return CCL::AI::RunStatus::Running; // 滞在中ずっと有効
    }
};

// ====================================================================
// 条件：ドローン残数で戦術を切り替える（ドローン数 = ボスHP）
// ====================================================================
/// @brief ドローン残数が指定値以下か。
/// @note  ドローン数の取得方法はプロジェクトに合わせて調整。
///        ここでは仮に BehaviorTreeComponent.blackboard を経由する想定。
///        実際は DroneManager リソース等から取得するのが綺麗。
struct DroneCountBelowCondition : public CCL::AI::ICondition
{
    struct Params
    {
        float count = 300.0f;
    };

    static const std::vector<CCL::AI::NodeParamField> &ParamFields()
    {
        static const std::vector<CCL::AI::NodeParamField> f = {
            NODE_PARAM_FLOAT(Params, count, "Drone Count <=", 300.0f, 0.0f, 1000.0f),
        };
        return f;
    }

    bool Evaluate(CCL::AI::ExecContext &ctx) override
    {
        const auto *p = static_cast<const Params *>(ctx.params);

        // 世界の事実（ドローン残数）を BattleContext から読む
        if (!ctx.world->HasResource<BattleContext>()) return false;
        auto &battle = ctx.world->GetResource<BattleContext>();

        // 残数が閾値以下なら true（= この戦術状態に入る）
        return static_cast<float>(battle.droneAliveCount) <= p->count;
    }
};

/// @brief HP割合が指定以下か（フェーズ/最終形態判定に使う）。
struct HealthBelowCondition : public CCL::AI::ICondition
{
    struct Params
    {
        float ratio = 0.5f;
    };

    static const std::vector<CCL::AI::NodeParamField> &ParamFields()
    {
        static const std::vector<CCL::AI::NodeParamField> f = {
            NODE_PARAM_FLOAT(Params, ratio, "HP Ratio <=", 0.5f, 0.0f, 1.0f),
        };
        return f;
    }

    bool Evaluate(CCL::AI::ExecContext &ctx) override
    {
        const auto *p  = static_cast<const Params *>(ctx.params);
        const auto *hp = ctx.world->GetComponent<HealthComponent>(ctx.entity);
        if (!hp || hp->maxHealth <= 0.0f)
            return false;
        return (hp->currentHealth / hp->maxHealth) <= p->ratio;
    }
};

// ====================================================================
// 登録
// ====================================================================
struct CommanderIDs
{
    CCL::AI::NodeTypeID setBtTask;
    CCL::AI::NodeTypeID droneCountBelow;
    CCL::AI::NodeTypeID healthBelow;
};

inline CommanderIDs RegisterNodes()
{
    auto        &reg = CCL::AI::NodeRegistry::Instance();
    CommanderIDs ids;
    ids.setBtTask       = reg.RegisterTask<SetActiveBTTask>("SetActiveBT", "Commander");
    ids.droneCountBelow = reg.RegisterCondition<DroneCountBelowCondition>("DroneCountBelow", "Tactics");
    ids.healthBelow     = reg.RegisterCondition<HealthBelowCondition>("HealthBelow", "Tactics");
    return ids;
}

// ====================================================================
// 司令官 StateTree アセット（ハードコード版・後で JSON 化可）
// ====================================================================
/// 戦術ツリー（子の順 = 優先度）:
///   Root
///    ├ Broken   : BREAK中             → BT "Broken"
///    ├ Final    : ドローン<=100        → BT "Final"
///    ├ Aggressive: ドローン<=400        → BT "Aggressive"
///    ├ Pressure : ドローン<=700        → BT "Pressure"
///    └ Control  : (フォールバック)      → BT "Control"
inline CCL::AI::StateTreeAsset BuildCommanderAsset(const CommanderIDs &ids)
{
    using namespace CCL::AI;
    StateTreeAsset asset;
    asset.name = "Commander";

    const uint16_t root = asset.AddState("Root");

    // 各戦術状態を作るヘルパー的な流れ
    auto addTactic = [&](const char *stateName, const char *btPath,
                         NodeTypeID condType, float condValue) -> uint16_t {
        const uint16_t s = asset.AddState(stateName, root);

        // 入場条件（フォールバックの Control は condType を ST_INVALID で渡す）
        if (condType != ST_INVALID_NODE_TYPE)
        {
            DroneCountBelowCondition::Params cp{condValue};
            asset.AddEnterCondition(s, condType,
                asset.AddParams(cp), sizeof(cp));
        }

        // SetActiveBT タスク（btPath を埋める）
        SetActiveBTTask::Params tp{};
        strncpy_s(tp.btPath, sizeof(tp.btPath), btPath, _TRUNCATE);
        asset.AddTask(s, ids.setBtTask,
            asset.AddParams(tp), sizeof(tp), 0);
        return s;
    };

    // 優先度の高い順に追加（上から評価される）
    addTactic("Final",      "Assets/BehaviorTree/TestAINode.json", ids.droneCountBelow, 100.0f);
    addTactic("Aggressive", "Assets/BehaviorTree/TestAINode.json", ids.droneCountBelow, 400.0f);
    addTactic("Pressure",   "Assets/BehaviorTree/TestAINode.json", ids.droneCountBelow, 700.0f);
    addTactic("Control",    "Assets/BehaviorTree/TestAINode.json", ST_INVALID_NODE_TYPE, 0.0f); // フォールバック


    (void)root;
    return asset;
}

} // namespace CommanderAI