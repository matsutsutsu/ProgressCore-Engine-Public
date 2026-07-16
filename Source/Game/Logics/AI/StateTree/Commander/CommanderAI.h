/// @file  CommanderAI.h
/// @brief ボス司令官の StateTree が使う Task/Condition の「語彙」定義。
///        木構造そのものは Assets/AI/Commander.json（StateTreeWindow で編集）に
///        あり、このファイルはノード実装(C++)だけを提供する。
///
/// 3層構造のうち「StateTree（戦術決定）」の層:
///   - Control / Pressure / Aggressive / Final などの戦術状態（木構造はJSON側）
///   - 各状態のタスク = SetActiveBTTask（使う BT のパスを書き換えるだけ）
///   - 実際の攻撃実行は BossAISystem（既存の BT 評価）が担う
///
/// ブリッジの仕組み:
///   SetActiveBTTask が BehaviorTreeComponent.assetPath を書き換える
///     → BossAISystem の既存ロード機構が新しい BT を自動ロード
///     → その BT が currentActionId を出力
///     → BossActionSystem が実行
///
/// ★木構造をここでハードコードしない理由★
///   RegisterNodes() でこのファイルの Task/Condition を NodeRegistry に
///   登録さえすれば、StateTreeWindow のパレットに自動で出てくる。
///   木構造（どの状態がどの順で並ぶか）は Assets/AI/Commander.json を
///   StateTreeWindow で編集し、シーン起動時に StateTreeSerializer::LoadDef +
///   Compile で読み込む（EditorScene::Initialize 参照）。
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

} // namespace CommanderAI