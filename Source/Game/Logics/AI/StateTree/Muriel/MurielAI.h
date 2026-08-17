/// @file  MurielAI.h
/// @brief ボス Muriel の StateTree が使う Task/Condition の「語彙」定義。
///        木構造そのものは Assets/AI/MurielAI.json（StateTreeWindow で編集）に
///        あり、このファイルはノード実装(C++)だけを提供する。
///
/// 3層構造のうち「StateTree（戦術決定）」の層:
///   - Combat / Dead などの大状態（木構造はJSON側）
///   - 各状態のタスク = SetActiveBTTask（使う BT のパスを書き換えるだけ）
///   - 実際の攻撃実行は BehaviorTreeSystem（既存の BT 評価）が担う
///
/// ブリッジの仕組み:
///   SetActiveBTTask が BehaviorTreeComponent.assetPath を書き換える
///     → BehaviorTreeSystem の既存ロード機構が新しい BT を自動ロード
///     → その BT が currentActionId を出力
///     → BossActionSystem が実行
///
/// 木構造をここでハードコードしない理由
///   RegisterNodes() でこのファイルの Task/Condition を NodeRegistry に
///   登録さえすれば、StateTreeWindow のパレットに自動で出てくる。
///   木構造（どの状態がどの順で並ぶか）は Assets/AI/MurielAI.json を
///   StateTreeWindow で編集し、シーン起動時に StateTreeSerializer::LoadDef +
///   Compile で読み込む（EditorScene::Initialize 参照）。
#pragma once
#include "Game/Logics/AI/StateTree/Core/StateTreeAsset.h"
#include "Game/Logics/AI/StateTree/Core/StateTreeNode.h"
#include "Game/Logics/AI/StateTree/Core/NodeRegistry.h"
#include "Game/Logics/AI/StateTree/Core/NodeParamField.h"
#include "Game/Logics/AI/Battle/BattleContext.h"

#include "Game/Logics/AI/BehaviorTree/Data/BehaviorTreeComponents.h" // BehaviorTreeComponent / BossCommandComponent
#include "Game/Logics/AI/Drone/DroneComponent.h"                    // DroneFormationType（死亡時のドローン収納）
#include "Game/Logics/Combat/HealthComponent.h"
#include "Game/Logics/Combat/DeadTag.h"                             // Tag::DeadTag（死亡判定）

#include "Engine/Animation/Components/AnimParametersComponent.h"    // SetTrigger（死亡アニメ）
#include "Engine/Core/Math/StringHash.h"                            // HashString
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/Platform/Core/Logger.h"

namespace MurielAI
{
// ====================================================================
// ブリッジタスク使う BehaviorTree を切り替える
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
            // パス型にすることで、エディタで「参照」ボタン → ファイルダイアログから
            // BT の .json を相対パスで選べる（直接テキスト入力も従来どおり可）。
            NODE_PARAM_PATH(Params, btPath, "BT Path", "", "BehaviorTreeNode"),
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
                "[Muriel] 戦術BT切替 → %s", p->btPath);
        }
        return CCL::AI::RunStatus::Running; // 滞在中ずっと有効
    }
};

// ====================================================================
// 条件：ドローン残数で戦術を切り替える（ドローン数 = ボスHP）
// ====================================================================
/// @brief ドローン残数が指定値以下か。
/// @note  残数は BehaviorTreeComponent.blackboard 経由で取得する。
///        DroneManager をリソース化して直接引く方が依存は浅くなるが、
///        条件ノードを Blackboard 以外に依存させたくないため現状は経由させている。
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
// 条件：死亡しているか（Dead 状態への入場条件）
// ====================================================================
/// @brief ボスが死亡したか。HealthSystem が HP<=0 で付与する DeadTag を見る。
/// @note  DeadTag 付与はダメージと同フレームに走るとは限らないため、
///        タグ未付与でも HP<=0 なら死亡とみなす保険を入れている。
///        これで「HP0になった瞬間」のフレームでも確実に Dead へ入れる。
struct IsDeadCondition : public CCL::AI::ICondition
{
    bool Evaluate(CCL::AI::ExecContext &ctx) override
    {
        if (ctx.world->HasComponent<Tag::DeadTag>(ctx.entity))
            return true;

        const auto *hp = ctx.world->GetComponent<HealthComponent>(ctx.entity);
        return hp && hp->currentHealth <= 0.0f;
    }
};

// ====================================================================
// タスク：死亡（終端状態）
//   EnterState で死亡アニメを1回だけ再生し、滞在中はボスの行動要求を止め、
//   群れを収納する（最後の攻撃指示を残さず綺麗に終わらせる）。
// ====================================================================
/// @brief 死亡状態の振る舞い。アニメ再生＋本体停止＋ドローン収納を行う。
/// @note  Tick は常に Running を返す＝この状態からは抜けない（死は終端）。
///        本体を完全静止させたい場合は BossActionSystem 側で DeadTag を見て
///        早期 return する処理を別途足すと確実（currentActionId=0 は Idle 相当）。
struct BossDeathTask : public CCL::AI::ITask
{
    struct Params
    {
        char triggerName[64] = "Death"; // 再生する死亡アニメのトリガー名
    };

    static const std::vector<CCL::AI::NodeParamField> &ParamFields()
    {
        static const std::vector<CCL::AI::NodeParamField> f = {
            NODE_PARAM_STRING(Params, triggerName, "Death Trigger", "Death"),
        };
        return f;
    }

    void EnterState(CCL::AI::ExecContext &ctx) override
    {
        const auto *p = static_cast<const Params *>(ctx.params);

        // 死亡アニメを1回だけ発火（EnterState はこの状態に入った瞬間のみ呼ばれる）。
        if (auto *anim = ctx.world->GetComponent<AnimParametersComponent>(ctx.entity))
            anim->SetTrigger(CCL::Utils::HashString(p ? p->triggerName : "Death"));

        CCL_LOG_INFO(LogCategory::AI, "[Muriel] ボス死亡 → Dead 状態へ");
    }

    CCL::AI::RunStatus Tick(CCL::AI::ExecContext &ctx) override
    {
        // 群れを収納し、本体の行動要求も止める（死んでいるのに攻撃が飛ぶのを防ぐ）。
        if (auto *cmd = ctx.world->GetComponent<BossCommandComponent>(ctx.entity))
        {
            cmd->droneAttackActive = false;
            cmd->requestFormation  = DroneFormationType::Hidden;
            cmd->currentActionId   = 0; // None → BossActionSystem は Idle
        }
        return CCL::AI::RunStatus::Running; // 死は終端。この状態から抜けない。
    }
};

// ====================================================================
// 登録
// ====================================================================
struct MurielAIIDs
{
    CCL::AI::NodeTypeID setBtTask;
    CCL::AI::NodeTypeID droneCountBelow;
    CCL::AI::NodeTypeID healthBelow;
    CCL::AI::NodeTypeID isDead;
    CCL::AI::NodeTypeID bossDeath;
};

inline MurielAIIDs RegisterNodes()
{
    auto        &reg = CCL::AI::NodeRegistry::Instance();
    MurielAIIDs  ids;
    ids.setBtTask       = reg.RegisterTask<SetActiveBTTask>("SetActiveBT", "Muriel");
    ids.droneCountBelow = reg.RegisterCondition<DroneCountBelowCondition>("DroneCountBelow", "Tactics");
    ids.healthBelow     = reg.RegisterCondition<HealthBelowCondition>("HealthBelow", "Tactics");
    ids.isDead          = reg.RegisterCondition<IsDeadCondition>("IsDead", "State");
    ids.bossDeath       = reg.RegisterTask<BossDeathTask>("BossDeath", "Boss");
    return ids;
}

} // namespace MurielAI