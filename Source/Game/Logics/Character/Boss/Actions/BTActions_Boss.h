/// @file  BTActions_Boss.h
/// @brief Drone Break 向け最小版。Move/Wait/Drone系のみ。
///        ボス固有のコンポーネント(BossCommand)は BTContext から受け取らず、
///          各アクションが world/entity から自分で取得する（汎用エンジン化のため）。
#pragma once
#include "Game/Logics/AI/BehaviorTree/Actions/BTAction.h"
#include "Game/Logics/AI/BehaviorTree/Actions/BTActionRegistry.h"
#include "Game/Logics/AI/BehaviorTree/Data/ActionRegistry.h"
#include "Game/Logics/Character/Boss/Components/BossCommandComponent.h"
#include "Game/Logics/AI/Drone/DroneComponent.h"
#include "Engine/ECS/Core/CCL_World.h"                               // world->GetComponent
#include "Engine/Animation/Components/AnimParametersComponent.h"     // SetTrigger（近接アニメ）
#include "Engine/Core/Math/StringHash.h"                             // HashString


namespace CCL::AI::BT::BossActions
{
// ====================================================================
// ① 待機系（汎用：bt の nodeTimers だけ使う。ボス非依存）
// ====================================================================
/// @brief 待つ。待ち時間はノードの param（秒）から取る。
/// @note  以前は「0.5秒」「1.0秒」…と12個の ActionID に分かれていた。
///        引数をIDで表すとIDが増える一方で、しかも中間の値を作れない。
struct WaitAction : public IBTAction
{
    BTNodeState Execute(BTContext &ctx) override
    {
        auto &timers = ctx.bt->nodeTimers;
        if (ctx.nodeId >= timers.size())
            return BTNodeState::Failure;

        if (timers[ctx.nodeId] == -1.0f)
            timers[ctx.nodeId] = ctx.param;

        if (timers[ctx.nodeId] > 0.0f)
            return BTNodeState::Running;

        return BTNodeState::Success;
    }
};

// ====================================================================
// ② 即時系：蓄積ダメージのリセット（汎用：bt のみ）
// ====================================================================
struct ResetDamageAction : public IBTAction
{
    BTNodeState Execute(BTContext &ctx) override
    {
        ctx.bt->blackboard.accumulatedDamage = 0.0f;
        return BTNodeState::Success;
    }
};

// ====================================================================
// ③ 移動（Move）ボス固有：BossCommand を自分で取りに行く
//    「移動してほしい」意図を currentActionId に書く。
//    タイマー 0.5s で一旦 Success させ、BT に再評価させる。
// ====================================================================
struct MoveAction : public IBTAction
{
    BTNodeState Execute(BTContext &ctx) override
    {
        auto &timers = ctx.bt->nodeTimers;
        if (ctx.nodeId >= timers.size())
            return BTNodeState::Failure;

        ctx.bt->lastLogActionId = ::CCL::AI::A_Move;

        // 完了
        if (timers[ctx.nodeId] == 0.0f)
            return BTNodeState::Success;

        // 未実行 → タイマー設定
        if (timers[ctx.nodeId] == -1.0f)
            timers[ctx.nodeId] = 0.5f;

        // 実行中：ボス固有の出力先を自分で取得して書く
        if (auto *cmd = ctx.world->GetComponent<BossCommandComponent>(ctx.entity)) {
            cmd->currentActionId = ::CCL::AI::A_Move;
        }
        return BTNodeState::Running;
    }
};

// ====================================================================
// ④ ドローン系（2000番台）ボス固有：BossCommand を自分で取りに行く
// ====================================================================
struct DroneAction : public IBTAction
{
    ActionID id;
    explicit DroneAction(ActionID actionId) : id(actionId) {}

    BTNodeState Execute(BTContext &ctx) override
    {
        ctx.bt->lastLogActionId = id;

        // 対応する陣形を引く。ActionRegistry.h の DRONE_ACTION_LIST が唯一の対応表。
        DroneFormationType formation = DroneFormationType::Hidden;
        if (!TryGetDroneFormation(id, formation)) return BTNodeState::Failure;

        // ボス固有の出力先（requestFormation）を自分で取得して書く
        if (auto *cmd = ctx.world->GetComponent<BossCommandComponent>(ctx.entity)) {
            cmd->requestFormation = formation;

            // 陣形の指示 = 「形を作る（予備動作/帰投）」フェーズなので攻撃信号は下ろす。
            // 突撃（リリース）は後続の DroneStrikeAction が明示的に立てる。
            cmd->droneAttackActive = false;
        }
        return BTNodeState::Success;
    }
};

// ====================================================================
// ⑤ ドローン攻撃リリース（Strike）ボス固有：BossCommand を自分で取りに行く
//    「今撃て」信号を立てる。DroneCommandSystem 経由で各ドローンが FireCharge へ。
// ====================================================================
struct DroneStrikeAction : public IBTAction
{
    BTNodeState Execute(BTContext &ctx) override
    {
        ctx.bt->lastLogActionId = ::CCL::AI::A_DroneStrike;
        if (auto *cmd = ctx.world->GetComponent<BossCommandComponent>(ctx.entity)) {
            cmd->droneAttackActive = true;
        }
        return BTNodeState::Success;
    }
};

// ====================================================================
// ⑥ 近接攻撃（Melee）ボス固有：アニメのトリガーを引く
//    設計：AIは「攻撃を決めてアニメのトリガーを引く」だけ。実際の当たり判定の
//    タイミングは、アニメクリップ内の HitBox イベント(startTime/endTime)が握る
//    （CombatAnimationSyncSystem が剣 Hitbox を区間ON/OFFする）。
//    このアクションは:
//      - 開始フレームに1回だけ SetTrigger（＝攻撃発生の合図）
//      - duration の間 Running（振り＋硬直ぶんBTを留める＝連打防止）
//      - currentActionId を立て続け、BossActionSystem が足を止める/踏み込む等の
//        物理挙動を担当する（薙ぎ払いは停止、将来の踏み込み/突進は前進）。
//    前提：ボスに AnimParameters コンポーネントと、triggerHash に対応する
//      遷移（例 "Slash"）が AnimStateMachine に用意されていること。
// ====================================================================
struct MeleeAttackAction : public IBTAction
{
    uint32_t triggerHash;
    float    defaultDuration; ///< ノードの param が未設定(0)のときに使う硬直
    ActionID id;

    MeleeAttackAction(uint32_t hash, float fallbackDur, ActionID actionId)
        : triggerHash(hash), defaultDuration(fallbackDur), id(actionId) {}

    BTNodeState Execute(BTContext &ctx) override
    {
        auto &timers = ctx.bt->nodeTimers;
        if (ctx.nodeId >= timers.size()) return BTNodeState::Failure;

        ctx.bt->lastLogActionId = id;

        // 硬直終了 → 完了
        if (timers[ctx.nodeId] == 0.0f) return BTNodeState::Success;

        // 開始フレーム：アニメのトリガーを1回だけ引く（＝攻撃発生の合図）
        if (timers[ctx.nodeId] == -1.0f)
        {
            // 硬直はノード側で調整できる。0 のままなら従来どおりの既定値を使う。
            timers[ctx.nodeId] = (ctx.param > 0.0f) ? ctx.param : defaultDuration;
            if (auto *anim = ctx.world->GetComponent<AnimParametersComponent>(ctx.entity))
                anim->SetTrigger(triggerHash);
        }

        // 硬直中：近接中であることを BossActionSystem へ伝える（停止/前進の制御に使う）
        if (auto *cmd = ctx.world->GetComponent<BossCommandComponent>(ctx.entity))
            cmd->currentActionId = id;

        return BTNodeState::Running;
    }
};

// ====================================================================
// 登録
// ====================================================================
inline void RegisterAll()
{
    auto &reg = BTActionRegistry::Instance();

    // 近接：薙ぎ払い（アニメのトリガー名 "Slash"）。硬直の既定は 0.8s で、
    //   ノードの param が入っていればそちらが勝つ。
    reg.RegisterActionInstance(::CCL::AI::A_BossSlash,
        std::make_unique<MeleeAttackAction>(CCL::Utils::HashString("Slash"), 0.8f, ::CCL::AI::A_BossSlash));

    reg.RegisterAction<WaitAction>(::CCL::AI::A_Wait);

    reg.RegisterAction<ResetDamageAction>(::CCL::AI::A_ResetDamage);
    reg.RegisterAction<MoveAction>(::CCL::AI::A_Move);
    reg.RegisterAction<DroneStrikeAction>(::CCL::AI::A_DroneStrike);

    // 陣形指示は全部おなじ DroneAction。ActionRegistry.h の表から直接登録するので、
    // 技を足しても消してもここは触らない（連番ループだと欠番も登録してしまう）。
#define CCL_REGISTER_DRONE_ACTION(Name, Id, Formation, Display)     reg.RegisterActionInstance(::CCL::AI::Name, std::make_unique<DroneAction>(::CCL::AI::Name));
    BT_DRONE_ACTION_LIST(CCL_REGISTER_DRONE_ACTION)
#undef CCL_REGISTER_DRONE_ACTION
}

} // namespace CCL::AI::BT::BossActions