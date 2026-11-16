#include "BossActionSystem.h"
#include "Actions/BossActionCommon.h"
#include "Actions/BossActionGeometry.h"

#include "Game/Logics/AI/Battle/BattleContext.h"
#include "Game/Logics/AI/BehaviorTree/Data/BTActionIds.h"
#include "Engine/Core/App/GameFlowState.h"
#include "Engine/Animation/Core/AnimStateMachine.h"
#include "Engine/Animation/Components/AnimParametersComponent.h"
#include "Engine/Animation/Components/RootMotionComponent.h"
#include "Engine/Core/Math/StringHash.h"
#include "Engine/ECS/Common/CCL_TypeInfo.h"

#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

#include <Jolt/Physics/Character/CharacterVirtual.h>

#include <cmath>
#include <algorithm>

#include <imgui.h>

using namespace CCL::ECS;
using namespace DirectX::SimpleMath;
using namespace BossActionCommon;
using namespace BossActionGeometry;

namespace
{
    // ひるみのステート（ValkyraAnimNode.json）。どれかを再生している間は、技を始めずにその場で止まる
    const uint32_t kFlinchStates[] = {CCL::Utils::HashString("Hit_F"), CCL::Utils::HashString("Hit_B"),
                                      CCL::Utils::HashString("Hit_L"), CCL::Utils::HashString("Hit_R")};

    // 技の開始で引くアニメのトリガー（ValkyraAnimNode.json）
    // 跳躍3種は同じ "Leap"。着地のクリップは BossAnimSyncSystem が切り替える
    const uint32_t kSideTravelLeftTrigger  = CCL::Utils::HashString("SideTravelLeft");
    const uint32_t kSideTravelRightTrigger = CCL::Utils::HashString("SideTravelRight");
    const uint32_t kBackStepTrigger        = CCL::Utils::HashString("BackStep");
    const uint32_t kLeapTrigger            = CCL::Utils::HashString("Leap");
    const uint32_t kDashTrigger            = CCL::Utils::HashString("Dash");
    const uint32_t kExhaustedTrigger       = CCL::Utils::HashString("Exhausted");

    // 体の状態に入った瞬間に引くトリガーを返す。0 は引かない
    // 待機・移動は、グラフが状態の値でクリップを選ぶ
    uint32_t StartTriggerOf(BossActionState state, bool repositionLeft)
    {
        switch (state)
        {
        case BossActionState::Reposition: return repositionLeft ? kSideTravelLeftTrigger : kSideTravelRightTrigger;
        case BossActionState::BackStep:   return kBackStepTrigger;
        case BossActionState::Leap:       return kLeapTrigger;
        case BossActionState::Dash:       return kDashTrigger;
        case BossActionState::Exhausted:  return kExhaustedTrigger;
        default:                          return 0;
        }
    }
} // namespace



// ---------------------------------------------------------------------------
// 入口
// ---------------------------------------------------------------------------

std::vector<CCL::ECS::TypeID> BossActionSystem::GetReadTypes() const
{
    auto types = Base::GetReadTypes();
    // テンプレート引数に足すと ForEach の絞り込みにもなり、アニメを持たないボスで動かなくなる
    types.push_back(CCL::ECS::TypeInfo<AnimStateMachineComponent>::ID()); // 止まりのモーションの長さと、ひるみ中か
    return types;
}



std::vector<CCL::ECS::TypeID> BossActionSystem::GetWriteTypes() const
{
    auto types = Base::GetWriteTypes();
    types.push_back(CCL::ECS::TypeInfo<AnimParametersComponent>::ID()); // 技の開始のトリガー
    types.push_back(CCL::ECS::TypeInfo<RootMotionComponent>::ID());     // 間合いを取る・様子見の間は足滑り補正を止める
    return types;
}



void BossActionSystem::Update(float rawDt)
{
    // 処理フロー:
    // 1. このフレームにやるべき動きを決める（演出中・ひるみ中なら待機）
    // 2. 動きが変わったフレームだけ、入ったときの処理とアニメの開始をする
    // 3. 今の動きを1フレーム分進める
    // 4. 間合いを取る・様子見の間は、足滑り補正を止める

    if (!m_world->HasResource<BattleContext>()) return;
    const auto &battle = m_world->GetResource<BattleContext>();
    if (!battle.HasPlayer()) return;

    const Vector3 playerPos = battle.playerPos; // プレイヤーの足元（ワールド）

    // 演出中・決着後かを、全個体ぶん1回だけ調べる
    // 決着後も止めるのは、倒れたプレイヤーへ技を出し続けないため
    const bool isCinematicOrMatchOver =
        m_world->HasResource<GameFlowState>() && m_world->GetResource<GameFlowState>().GameplayFrozen();

    ForEachWithID([&](EntityID                            entity,
                      CharacterMovementComponent         &movement,
                      BossActionComponent                &bossAction,
                      TransformComponent                 &transform,
                      const BossCommandComponent         &command,
                      const JoltCharacterConfigComponent &joltConfig,
                      const JoltCharacterHandleComponent &joltHandle,
                      const TimeStateComponent           &timeState) {
        // この個体の時計の dt（ヒットストップ・スローを含む）
        const float dt = timeState.localDt;

        // ---------------------------------------------------------
        // 1. このフレームの動き
        // ---------------------------------------------------------

        // ひるむかはアニメグラフが決める（待機・移動・様子見の間だけ入れる）。ここは結果を読むだけ
        const auto *animStateMachine = m_world->GetComponent<AnimStateMachineComponent>(entity);
        UpdateFlinch(bossAction, animStateMachine, dt);

        // BT の要求を体の動きに変える
        // 演出中・決着後・ひるみ中は、要求を読まずに待機にする
        // ひるみ中は依頼番号を進めないので、明けたときに BT が要求している技へ入り直す
        BossActionState nextState;
        if (isCinematicOrMatchOver || bossAction.isFlinching)
        {
            nextState = BossActionState::None;
        }
        else
        {
            // ActionID の要求を、体の動きと跳び方に変換する
            const TranslatedAction translated = TranslateAction(command.currentActionId, bossAction);
            nextState                         = translated.state;

            // このフレームのアクションに必要な情報をまとめる
            // raging は BossPhaseSystem がフレームの最初に写した値（前のフレームに怒りの行動が評価されたか）
            const BossActionFrame frame{transform, movement, joltConfig, joltHandle, battle, playerPos, dt, animStateMachine, command.raging};

            // ---------------------------------------------------------
            // 2. 動きに入った瞬間
            // ---------------------------------------------------------

            // 状態が変わった瞬間に処理が走る
            // アニメーションやアクションのはじめだけの処理に使う
            // 依頼番号も見るのは、同じ技を続けて出したときに始め直すため
            if (nextState != bossAction.currentState || command.actionSerial != bossAction.actionSerial)
            {
                // 状態遷移時に１回だけ呼ばれる
                bossAction.actionSerial = command.actionSerial;
                OnEnterState(nextState, translated.leapDef, command.currentActionParam, bossAction, frame);

                // アクション状態に合わせたアニメのトリガーを引く
                // 間合いを取るの左右は OnEnterState が決めた後に読む
                const uint32_t startTrigger = StartTriggerOf(nextState, bossAction.repositionLeft);

                auto *animParams = m_world->GetComponent<AnimParametersComponent>(entity);

                // アニメーションがあって、かつトリガーが設定されていればAnimationのトリガーを引く
                if (startTrigger != 0 && animParams) animParams->SetTrigger(startTrigger);
            }

            // 状態を更新して、経過時間を加算する
            bossAction.currentState = nextState;
            bossAction.actionTimer += dt;

            // ---------------------------------------------------------
            // 3. 1フレーム分進める
            // ---------------------------------------------------------

            switch (bossAction.currentState)
            {
            case BossActionState::Move:
                m_move.UpdateMove(bossAction, frame);
                break;
            case BossActionState::Reposition:
                m_reposition.Update(bossAction, frame);
                break;
            case BossActionState::BackStep:
                m_backStep.Update(bossAction, frame);
                break;
            case BossActionState::Leap:
                m_leap.Update(bossAction, frame);
                break;
            case BossActionState::Dash:
                m_dash.Update(bossAction, frame);
                break;
            case BossActionState::Footwork:
                m_footwork.Update(bossAction, frame);
                break;
            case BossActionState::Exhausted:
                // 足を止め、向き直らずに決めた長さだけ待つ。疲れている間は相手を追わない
                UpdateRecovery(bossAction, bossAction.exhaustedDuration, frame);
                break;
            default:
                m_move.UpdateIdle(bossAction, frame);
                break;
            }

            // ---------------------------------------------------------
            // 4. 足滑り補正
            // ---------------------------------------------------------

            // 間合いを取る・様子見のループのクリップは飛んで進む動きで、足が地面に着かない
            // 足滑り補正（移動の速さに合わせて再生を速める）を掛けると、ただ速く回るだけになる
            if (auto *rootMotion = m_world->GetComponent<RootMotionComponent>(entity))
            {
                const bool isFlying =
                    (bossAction.currentState == BossActionState::Reposition || bossAction.currentState == BossActionState::Footwork);
                rootMotion->externallyDriven = isFlying;
            }
        }
    });
}



void BossActionSystem::OnGui()
{
    auto bossView = m_world->View<TransformComponent, BossActionComponent>();
    if (bossView.begin() == bossView.end())
    {
        ImGui::TextDisabled("Boss entity not found in scene.");
        return;
    }

    for (auto bossEntity : bossView)
    {
        auto *transform = m_world->GetComponent<TransformComponent>(bossEntity);
        if (!transform) continue;

        ImGui::PushID(static_cast<int>(bossEntity));
        ImGui::Text("Boss Entity ID: %llu", (unsigned long long)bossEntity);

        // 動かしたら瞬間移動として物理にも伝える
        float posInput[3] = {transform->position.x, transform->position.y, transform->position.z};
        if (ImGui::DragFloat3("Boss Pos (Warp)", posInput, 0.2f))
        {
            transform->position     = DirectX::SimpleMath::Vector3(posInput[0], posInput[1], posInput[2]);
            transform->isTeleported = true;
            transform->isDirty      = true;
        }

        ImGui::PopID();
    }
}



// ---------------------------------------------------------------------------
// 技の切り替え
// ---------------------------------------------------------------------------

BossActionSystem::TranslatedAction BossActionSystem::TranslateAction(ActionID actionId, const BossActionComponent &bossAction) const
{
    switch (actionId)
    {
    case A_Move:           return {BossActionState::Move};
    case A_BossReposition: return {BossActionState::Reposition};
    case A_BossBackStep:   return {BossActionState::BackStep};
    case A_BossLeapEscape: return {BossActionState::Leap, &bossAction.escapeLeap};
    case A_BossLeapAttack: return {BossActionState::Leap, &bossAction.attackLeap};
    case A_BossAirHover:   return {BossActionState::Leap, &bossAction.hoverLeap};
    case A_BossDashAttack: return {BossActionState::Dash};
    case A_BossFootwork:   return {BossActionState::Footwork};
    case A_BossExhausted:  return {BossActionState::Exhausted};
    default:               return {BossActionState::None};
    }
}



void BossActionSystem::UpdateFlinch(BossActionComponent &bossAction, const AnimStateMachineComponent *animStateMachine,
                                    float dt) const
{
    bossAction.isFlinching = false;
    if (animStateMachine)
    {
        for (uint32_t flinchState : kFlinchStates)
        {
            if (animStateMachine->currentStateHash == flinchState)
            {
                bossAction.isFlinching = true;
                break;
            }
        }
    }

    // 冷却は明けてから数えるので、ひるんでいる間は満タンに保つ
    // dt はこの個体の時計なので、ヒットストップ中は冷却も止まる
    if (bossAction.isFlinching)
    {
        bossAction.flinchCooldownTimer = bossAction.flinchCooldown;
    }
    else if (bossAction.flinchCooldownTimer > 0.0f)
    {
        bossAction.flinchCooldownTimer -= dt;
    }
}



void BossActionSystem::OnEnterState(BossActionState nextState, const BossLeapDef *leapDef, float actionParam,
                                    BossActionComponent &bossAction, const BossActionFrame &frame)
{
    bossAction.actionTimer    = 0.0f;
    bossAction.recoveryTimer  = 0.0f;
    bossAction.actionFinished = false;

    // 前の技の移動の予約を取り消す
    // 技が途中で切り替わっても、前の技の終点まで運ばれ続けない
    frame.movement.CancelMove();

    // 進む向きを、相手への向きで固定する
    // 以後は相手が動いても追わないので、動く技を避けられる
    Vector3     toPlayerDir      = FlatToward(frame.transform.position, frame.playerPos);
    const float distanceToPlayer = toPlayerDir.Length();
    if (distanceToPlayer > 0.001f)
    {
        toPlayerDir /= distanceToPlayer;
        bossAction.lockedDirection = toPlayerDir;
    }

    // 技ごとの準備
    switch (nextState)
    {
    case BossActionState::BackStep:
        m_backStep.Enter(bossAction, frame);
        break;
    case BossActionState::Reposition:
        m_reposition.Enter(actionParam, bossAction, frame);
        break;
    case BossActionState::Leap:
        m_leap.Enter(leapDef, bossAction);
        break;
    case BossActionState::Dash:
        m_dash.Enter(bossAction, frame);
        break;
    case BossActionState::Footwork:
        m_footwork.Enter(bossAction, frame);
        break;
    default:
        // 待機・移動・疲れは、入るときの準備が要らない
        break;
    }
}



// BehaviorTreeSystem(L02) の後、JoltCharacterUpdateSystem(L03) の前
REGISTER_LOGIC_SYSTEM(BossActionSystem, Priority::LogicStage::L02_PostUpdate);
