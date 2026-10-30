#include "BossActionSystem.h"

#include "Game/Logics/AI/Battle/BattleContext.h"
#include "Game/Logics/AI/BehaviorTree/Data/BTActionIds.h"
#include "Engine/Core/App/GameFlowState.h"                           // cinematic
#include "Engine/Animation/Core/AnimStateMachine.h"                  // 止まりのモーションの長さ
#include "Engine/Animation/Components/AnimParametersComponent.h"     // 技の開始のトリガー
#include "Engine/Animation/Components/RootMotionComponent.h"         // 横移動中は足滑り補正を止める
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

namespace
{
    // 角度（rad）を -π〜π に収める
    // yaw の差を、近い回り方で取るために使う
    float WrapAngle(float angle)
    {
        while (angle > DirectX::XM_PI)
            angle -= DirectX::XM_2PI;
        while (angle < -DirectX::XM_PI)
            angle += DirectX::XM_2PI;
        return angle;
    }



    // from から to への水平の向き（正規化しない）
    Vector3 FlatToward(const Vector3 &from, const Vector3 &to)
    {
        Vector3 flatOffset = to - from;
        flatOffset.y       = 0.0f;
        return flatOffset;
    }



    // 向きたい方向に、turnSpeed の速さで回転する
    void FaceToward(TransformComponent &transform, const Vector3 &lookDir, float turnSpeed, float dt,
                    const JoltCharacterHandleComponent &joltHandle)
    {
        if (lookDir.LengthSquared() <= 0.001f) return;

        // 水平の向きだけで yaw を計算する
        float targetYaw = atan2f(lookDir.x, lookDir.z);

        // 今の yaw を計算する
        Vector3 currentForward = Vector3::Transform(Vector3::UnitZ, transform.rotation);
        float   currentYaw     = atan2f(currentForward.x, currentForward.z);

        // 近い回り方で yaw を変える
        float yawDiff = WrapAngle(targetYaw - currentYaw);
        float newYaw  = currentYaw + yawDiff * (std::min)(turnSpeed * dt, 1.0f);

        SetCharacterRotation(transform, joltHandle, Quaternion::CreateFromAxisAngle(Vector3::UnitY, newYaw));
    }



    // 予約した移動の速さの上限の既定値 [m/s]（MoveToOptions::maxSpeed と同じ）
    // 突進はこれより速く突っ込むので、突進の速さに合わせて上げる
    constexpr float kMaxTrackSpeed = 150.0f;



    // ステージの壁から離す距離 [m]。カプセル半径（1.4m）に余裕を足した値
    // 着地点や下がる先が、この内側の円に収まるようにする
    constexpr float kWallMargin = 3.0f;



    // 跳躍の頂点の高さを、相手との距離で上下させる範囲
    // 基準は BossLeapDef::height。近い相手へは低く、遠い相手へは高く跳ぶ
    constexpr float kLeapHeightNearDistance = 10.0f;  // この距離以下で一番低くなる [m]
    constexpr float kLeapHeightFarDistance  = 80.0f;  // この距離以上で一番高くなる [m]
    constexpr float kLeapHeightOffsetMin    = -10.0f; // 一番近いときに基準へ足す高さ [m]
    constexpr float kLeapHeightOffsetMax    = 10.0f;  // 一番遠いときに基準へ足す高さ [m]



    // 間合いを取る移動の向きの決め方
    // 前後へはこの角度までしか傾けない。アニメが左右の横移動しか無いため
    constexpr float kRepositionMaxTiltDeg  = 40.0f; // 真横からの傾きの上限 [度]
    constexpr float kRepositionRadialRange = 10.0f; // 保ちたい距離からこれだけずれると、前後が横と同じ強さ [m]



    // 様子見の回る側とアニメの向きの決め方
    // アニメの向きは、斜め45度の付近で毎フレーム入れ替わらないよう、差が付いたときだけ切り替える
    constexpr float kFootworkSideLookAhead     = 30.0f; // 回る側を決めるとき、左右の空きを見る距離 [m]
    constexpr float kFootworkAnimDirHysteresis = 0.2f;  // 今の向きより、内積がこれだけ大きい向きへ切り替える



    // アニメのステートが1回流れ切る長さ（クリップの長さ÷再生速度）を返す
    // サブマシンの中も探す。見つからなければ fallback
    float StatePlayTime(const AnimStateGraph &graph, uint32_t stateHash, float fallback)
    {
        for (const AnimState &state : graph.states)
        {
            if (state.stateHash == stateHash && state.sequence && state.playbackSpeed > 0.0f)
                return state.sequence->duration / state.playbackSpeed;
            if (state.isSubMachine && state.subGraph)
            {
                const float subPlayTime = StatePlayTime(*state.subGraph, stateHash, -1.0f);
                if (subPlayTime >= 0.0f) return subPlayTime;
            }
        }
        return fallback;
    }



    // 後方ステップの止まりのモーションを流すステート
    // （ValkyraAnimNode.json の BackStepStop）
    const uint32_t kBackStepStopState = CCL::Utils::HashString("BackStepStop");

    // 技の開始で引くアニメのトリガー（ValkyraAnimNode.json）
    // 跳躍3種は同じ "Leap"。着地のクリップは BossAnimSyncSystem が切り替える
    const uint32_t kSideTravelLeftTrigger  = CCL::Utils::HashString("SideTravelLeft");
    const uint32_t kSideTravelRightTrigger = CCL::Utils::HashString("SideTravelRight");
    const uint32_t kBackStepTrigger        = CCL::Utils::HashString("BackStep");
    const uint32_t kLeapTrigger            = CCL::Utils::HashString("Leap");
    const uint32_t kDashTrigger            = CCL::Utils::HashString("Dash");

    // 体の状態に入った瞬間に引くトリガーを返す。0 は引かない
    // 待機・移動は、グラフが状態の値でクリップを選ぶ
    uint32_t StartTriggerOf(BossActionState state, bool sideTravelLeft)
    {
        switch (state)
        {
        case BossActionState::SideTravel: return sideTravelLeft ? kSideTravelLeftTrigger : kSideTravelRightTrigger;
        case BossActionState::BackStep:   return kBackStepTrigger;
        case BossActionState::Leap:       return kLeapTrigger;
        case BossActionState::Dash:       return kDashTrigger;
        default:                          return 0;
        }
    }
} // namespace



// ---------------------------------------------------------------------------
// 入口
// ---------------------------------------------------------------------------

// テンプレート引数以外に読む型の申告
std::vector<CCL::ECS::TypeID> BossActionSystem::GetReadTypes() const
{
    auto types = Base::GetReadTypes();
    types.push_back(CCL::ECS::TypeInfo<AnimStateMachineComponent>::ID()); // 止まりのモーションの長さ
    return types;
}



// テンプレート引数以外に書く型の申告
std::vector<CCL::ECS::TypeID> BossActionSystem::GetWriteTypes() const
{
    auto types = Base::GetWriteTypes();
    types.push_back(CCL::ECS::TypeInfo<AnimParametersComponent>::ID()); // 技の開始のトリガー
    types.push_back(CCL::ECS::TypeInfo<RootMotionComponent>::ID());     // 横移動中は足滑り補正を止める
    return types;
}



// ボス全員の、技の切り替えと1フレーム分の動き
void BossActionSystem::Update(float rawDt)
{
    // 処理フロー:
    // 1. このフレームにやるべき動きを決める（演出中なら待機）
    // 2. 動きが変わったフレームだけ、入ったときの処理とアニメの開始をする
    // 3. 今の動きを1フレーム分進める
    // 4. 横移動の間は、足滑り補正を止める

    if (!m_world->HasResource<BattleContext>()) return;
    const auto &battle = m_world->GetResource<BattleContext>();
    if (!battle.HasPlayer()) return;

    const Vector3 playerPos(battle.playerPos); // プレイヤーの足元（ワールド）

    // 演出中・決着後かを、全個体ぶん1回だけ調べる
    // 決着後も止めるのは、倒れたプレイヤーへ技を出し続けないため
    const GameFlowState *gameFlow               = m_world->HasResource<GameFlowState>() ? &m_world->GetResource<GameFlowState>() : nullptr;
    const bool           isCinematicOrMatchOver = gameFlow && (gameFlow->cinematic || gameFlow->matchOver);

    ForEachWithID([&](EntityID                            entity,
                      CharacterMovementComponent         &movement,
                      BossActionComponent                &bossAction,
                      TransformComponent                 &transform,
                      const BossCommandComponent         &command,
                      const JoltCharacterConfigComponent &joltConfig,
                      const JoltCharacterHandleComponent &joltHandle,
                      const TimeStateComponent           &timeState) {
        if (!joltHandle.character) return;

        // この個体の時計の dt（ヒットストップ・スローを含む）
        const float dt = timeState.localDt;

        // ---------------------------------------------------------
        // 1. このフレームの動き
        // ---------------------------------------------------------

        // BT の要求を体の動きに変える
        // 演出中・決着後は、要求を読まずに待機にする
        BossActionState nextState;
        if (isCinematicOrMatchOver)
        {
            nextState = BossActionState::None;
        }
        else
        {
            // ActionIDの要求を、BossActionStateに変換する
            nextState = TranslateAction(command.currentActionId);
        }

        // このフレームのアクションに必要な情報をまとめる
        const Frame frame{transform, movement, joltConfig, joltHandle, battle, playerPos, dt,
                      m_world->GetComponent<AnimStateMachineComponent>(entity)};

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
            OnEnterState(nextState, command.currentActionId, command.currentActionParam, bossAction, frame);

            // アクション状態に合わせたアニメのトリガーを引く
            // 横移動の左右は OnEnterState が決めた後に読む
            const uint32_t startTrigger = StartTriggerOf(nextState, bossAction.sideTravelLeft);

            auto          *animParams   = m_world->GetComponent<AnimParametersComponent>(entity);

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
            ProcessMove(bossAction, frame);
            break;
        case BossActionState::SideTravel:
            ProcessSideTravel(bossAction, frame);
            break;
        case BossActionState::BackStep:
            ProcessBackStep(bossAction, frame);
            break;
        case BossActionState::Leap:
            ProcessLeap(bossAction, frame);
            break;
        case BossActionState::Dash:
            ProcessDash(bossAction, frame);
            break;
        case BossActionState::Footwork:
            ProcessFootwork(bossAction, frame);
            break;
        default:
            ProcessIdle(bossAction, frame);
            break;
        }

        // ---------------------------------------------------------
        // 4. 足滑り補正
        // ---------------------------------------------------------

        // 横移動・様子見のループのクリップは飛んで進む動きで、足が地面に着かない
        // 足滑り補正（移動の速さに合わせて再生を速める）を掛けると、ただ速く回るだけになる
        if (auto *rootMotion = m_world->GetComponent<RootMotionComponent>(entity))
        {
            const bool isFlying =
                (bossAction.currentState == BossActionState::SideTravel || bossAction.currentState == BossActionState::Footwork);
            rootMotion->externallyDriven = isFlying;
        }
    });
}



// デバッグ用。ボスの位置をインスペクタから直接動かす
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
// 移動系の状態
// ---------------------------------------------------------------------------

// ボスの待機。足を止め、相手の方へゆっくり向き直る
void BossActionSystem::ProcessIdle(BossActionComponent &bossAction, const Frame &frame)
{
    // 足を止める
    // 止まり方の慣性は、物理側（Jolt の deceleration）が付ける
    frame.movement.desiredVelocity = Vector3::Zero;

    // 相手の方へ、移動中より遅く向き直る
    // すぐ正対すると、背後を取る意味が無くなる
    FaceToward(frame.transform, FlatToward(frame.transform.position, frame.playerPos), bossAction.idleTurnSpeed, frame.dt,
               frame.joltHandle);
}



// ボスの移動。Jolt の walkSpeed で相手へ向かって歩く
void BossActionSystem::ProcessMove(BossActionComponent &bossAction, const Frame &frame)
{
    Vector3 toPlayerDir = FlatToward(frame.transform.position, frame.playerPos);

    // 相手と重なっていて向きが決まらないときは止まる
    if (toPlayerDir.LengthSquared() <= 0.001f)
    {
        frame.movement.desiredVelocity = Vector3::Zero;
        return;
    }

    toPlayerDir.Normalize();

    // 相手の方へ歩く
    // overrideSpeed を書かないので、物理側が Jolt の walkSpeed で動かす
    frame.movement.desiredVelocity = toPlayerDir;

    // toPlayerDir の方向へ、turnSpeed の速さで向き直る
    FaceToward(frame.transform, toPlayerDir, bossAction.turnSpeed, frame.dt, frame.joltHandle);
}



// ---------------------------------------------------------------------------
// 行動の切り替え
// ---------------------------------------------------------------------------

// BT の技(ActionID)を、体の動き(BossActionState)に変える
// 知らない技は待機(None)
BossActionState BossActionSystem::TranslateAction(ActionID actionId) const
{
    if (actionId == A_Move) return BossActionState::Move;
    if (actionId == A_BossSideTravelLeft) return BossActionState::SideTravel;
    if (actionId == A_BossSideTravelRight) return BossActionState::SideTravel;
    if (actionId == A_BossReposition) return BossActionState::SideTravel;
    if (actionId == A_BossBackStep) return BossActionState::BackStep;
    if (actionId == A_BossLeapEscape) return BossActionState::Leap;
    if (actionId == A_BossLeapAttack) return BossActionState::Leap;
    if (actionId == A_BossAirHover) return BossActionState::Leap;
    if (actionId == A_BossDashAttack) return BossActionState::Dash;
    if (actionId == A_BossFootwork) return BossActionState::Footwork;
    return BossActionState::None;
}



// 跳躍の技(ActionID)に対応する跳び方を返す。跳躍の技でなければ nullptr
// 跳躍を増やすときは、ここに1行と BossActionComponent に BossLeapDef を1つ足す
const BossLeapDef *BossActionSystem::LeapDefOf(ActionID actionId, const BossActionComponent &bossAction) const
{
    if (actionId == A_BossLeapEscape) return &bossAction.escapeLeap;
    if (actionId == A_BossLeapAttack) return &bossAction.attackLeap;
    if (actionId == A_BossAirHover) return &bossAction.hoverLeap;
    return nullptr;
}



// 状態遷移に入った瞬間呼ばれる処理
void BossActionSystem::OnEnterState(BossActionState nextState, ActionID actionId, float actionParam,
                                    BossActionComponent &bossAction, const Frame &frame)
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

    switch (nextState)
    {
    case BossActionState::BackStep: {
        // 後方ステップの準備。進む向きを相手と逆にする
        bossAction.lockedDirection = -bossAction.lockedDirection;
        bossAction.backStepPhase   = BossBackStepPhase::Retreat;

        // 止まりのモーションの長さを、アニメグラフから取る
        // 下がる長さ＝下がり始め＋止まりのモーション。取れなければ 0.4 秒
        bossAction.backStepStopTime = frame.animStateMachine ? StatePlayTime(frame.animStateMachine->internalGraph, kBackStepStopState, 0.4f) : 0.4f;

        const float retreatTime      = (std::max)(0.0f, bossAction.backStepRetreatTime);
        const float backStepMoveTime = (std::max)(0.05f, retreatTime + bossAction.backStepStopTime);

        // まっすぐ後ろの終点まで運ぶ予約をする
        // 速く下がり始めて終点で止まり、途中で少し浮いて地面に戻る
        const Vector3 bossPos        = GetCharacterWorldPosition(frame.joltHandle);
        const Vector3 backStepEndPos = StraightPointInArena(bossPos, bossAction.lockedDirection, bossAction.backStepDistance, frame.battle);
        frame.movement.MoveTo(backStepEndPos, backStepMoveTime, {.height = bossAction.backStepHeight, .ease = MoveEase::EaseOut});
        break;
    }

    case BossActionState::SideTravel:
        // 横移動の準備。向きを決め、動き出しの移動を予約する
        // 間合いを取る技は、向きを距離と壁から計算する。それ以外は技の左右で真横
        if (actionId == A_BossReposition)
        {
            BeginTravel(RepositionDirection(actionParam, bossAction, frame), actionParam, bossAction, frame);
        }
        else
        {
            BeginSideTravel(actionId, actionParam, bossAction, frame);
        }
        break;

    case BossActionState::Leap:
        // 跳躍の準備。どの技かで跳び方（着地点の決め方・着地で攻撃するかを含む）を写す
        // 着地点そのものは、踏み切りの瞬間に TakeOffLeap が決める
        bossAction.leapPhase = BossLeapPhase::Windup;
        if (const BossLeapDef *selectedLeapDef = LeapDefOf(actionId, bossAction))
        {
            bossAction.leapDef = *selectedLeapDef;
        }
        break;

    case BossActionState::Dash: {
        // 突進の準備。段を最初に戻す
        // 突っ込む方向は、構えに入る瞬間に LockDashAim が決める
        bossAction.dashPhase = BossDashPhase::Retreat;

        // 下がって狙う地点（空中）を決める。まっすぐ後ろへ下がり、浮いたまま止まる
        // lockedDirection は相手への向きなので、その逆へ下がる
        const Vector3 bossPos = GetCharacterWorldPosition(frame.joltHandle);
        Vector3       aimPos  = StraightPointInArena(bossPos, -bossAction.lockedDirection, bossAction.dashRetreatDistance, frame.battle);
        aimPos.y              = bossPos.y + bossAction.dashRetreatHeight;

        bossAction.dashStartPos = bossPos;
        bossAction.dashAimPos   = aimPos;

        // 狙う地点まで、速く下がり始めて止まる移動を予約する
        const float retreatTime = (std::max)(0.05f, bossAction.dashRetreatTime);
        frame.movement.MoveTo(bossAction.dashAimPos, retreatTime, {.ease = MoveEase::EaseOut});
        break;
    }

    case BossActionState::Footwork:
        // 様子見の準備。長さを引き、相手の周りを回る側を決める
        BeginFootwork(bossAction, frame);
        break;

    default:
        // 待機・移動は、入るときの準備が要らない
        break;
    }
}



// ---------------------------------------------------------------------------
// 技の状態
// ---------------------------------------------------------------------------

// ボスの後方ステップ。相手を向いたまま素早く下がり、着地して硬直する
// 下がる移動そのものは、OnEnterState で予約した MoveTo が運ぶ
void BossActionSystem::ProcessBackStep(BossActionComponent &bossAction, const Frame &frame)
{
    // 入った瞬間の相手の方向を向き続ける（後から追わない）
    // 背を向けると、間合いを取り直す技ではなく逃げたように見える
    FaceToward(frame.transform, -bossAction.lockedDirection, bossAction.turnSpeed, frame.dt, frame.joltHandle);

    // 下がり始め → 止まり の切り替えは、アニメのために時刻で決める
    const bool isRetreatTimeOver = bossAction.actionTimer >= bossAction.backStepRetreatTime;
    if (bossAction.backStepPhase == BossBackStepPhase::Retreat && isRetreatTimeOver)
        bossAction.backStepPhase = BossBackStepPhase::Stop;

    // 予約した移動が終わったら、着地硬直にする
    if (!frame.movement.IsMoving()) bossAction.backStepPhase = BossBackStepPhase::Recovery;

    if (bossAction.backStepPhase == BossBackStepPhase::Recovery)
    {
        ProcessRecovery(bossAction, bossAction.backStepRecovery, frame);
        return;
    }

    bossAction.moveDirection = bossAction.lockedDirection;
}



// ボスの横移動。相手を向いたまま、入った瞬間に決めた真横へ進む
// 加速 → 最高速 → 減速。各段の移動は予約が運び、ここでは段の切り替えと向きだけを決める
void BossActionSystem::ProcessSideTravel(BossActionComponent &bossAction, const Frame &frame)
{
    // --- 段の切り替え ---

    // 予約した移動が終わったら、次の段の移動を予約する
    // 加速の移動は、入った瞬間に BeginSideTravel が予約済み
    if (!frame.movement.IsMoving())
    {
        switch (bossAction.sideTravelPhase)
        {
        case BossSideTravelPhase::Start:
            // 最高速のまま、残りの距離を進む
            // 残りが無ければ減速へ直接進む（最高速の1フレームを挟むと、その分だけ進みすぎる）
            if (bossAction.sideTravelCruiseTime > 0.0f)
            {
                frame.movement.MoveInDirection(bossAction.lockedDirection, bossAction.sideTravelSpeed,
                                           bossAction.sideTravelCruiseTime, bossAction.sideTravelSpeed);
                bossAction.sideTravelPhase = BossSideTravelPhase::Travel;
                break;
            }

            // 意図的にreturn書いてない、すぐにTravelに進みたいため
            [[fallthrough]];

        case BossSideTravelPhase::Travel:
            // 最高速から 0 へ減速して止まる
            frame.movement.MoveInDirection(bossAction.lockedDirection, bossAction.sideTravelSpeed,
                                       bossAction.sideTravelStopTime, 0.0f);
            bossAction.sideTravelPhase = BossSideTravelPhase::Stop;
            break;

        default:
            // 止まった。速度は予約が 0 にして止める
            bossAction.sideTravelPhase = BossSideTravelPhase::End;
            break;
        }
    }

    // --- 終わり ---

    // 硬直は持たない。止まりのモーションがそのまま後隙になる
    if (bossAction.sideTravelPhase == BossSideTravelPhase::End)
    {
        ProcessRecovery(bossAction, 0.0f, frame);
        return;
    }

    // --- 向き ---

    // 体は相手を向き続ける。進む向きは入った瞬間のまま変えない
    FaceToward(frame.transform, FlatToward(frame.transform.position, frame.playerPos), bossAction.turnSpeed, frame.dt, frame.joltHandle);

    bossAction.moveDirection = bossAction.lockedDirection;
}



// ボスの跳躍。溜め → 放物線で飛ぶ → 着地硬直
// 跳躍3種はすべてここを通り、違いは leapDef（跳び方）だけ。飛ぶ移動は予約が運ぶ
void BossActionSystem::ProcessLeap(BossActionComponent &bossAction, const Frame &frame)
{
    // 処理フロー:
    // 1. 溜め（時間が来たら踏み切る）
    // 2. 予約した移動が終わったら、次の段へ進む
    // 3. 空中なら向きを、着地した後なら硬直を進める

    if (bossAction.leapPhase == BossLeapPhase::Windup)
    {
        ProcessLeapWindup(bossAction, frame);

        // まだ溜めている。踏み切ったフレームは、そのまま空中の処理へ進む
        if (bossAction.leapPhase == BossLeapPhase::Windup) return;
    }

    if (!frame.movement.IsMoving()) AdvanceLeapPhase(bossAction, frame);

    if (bossAction.leapPhase == BossLeapPhase::Recovery)
    {
        ProcessLeapLanding(bossAction, frame);
    }
    else
    {
        ProcessLeapAir(bossAction, frame);
    }
}



// 跳躍の溜め。足を止めて相手を向き、溜めの時間が来たら踏み切る
// 着地点はまだ決めない（溜めを見てから動いた相手に付いていかない）
void BossActionSystem::ProcessLeapWindup(BossActionComponent &bossAction, const Frame &frame)
{
    // 溜め中は、相手の方を向き続ける
    frame.movement.desiredVelocity = Vector3::Zero;
    FaceToward(frame.transform, FlatToward(frame.transform.position, frame.playerPos), bossAction.turnSpeed, frame.dt, frame.joltHandle);

    // 溜めの時間が来るまで、飛ぶ移動の予約をしない
    if (bossAction.actionTimer < bossAction.leapDef.windup) return;

    TakeOffLeap(bossAction, frame);
}



// 跳躍の踏み切り。着地点と飛ぶ向きを決め、最初の移動を予約する
// 滞空は「頂点へ上がる」だけを予約し、留まる・降りるは AdvanceLeapPhase が続ける
void BossActionSystem::TakeOffLeap(BossActionComponent &bossAction, const Frame &frame) const
{
    // 踏み切った地点を覚える（跳躍中のドローンの陣形の高さに使う）
    const Vector3 takeoffPos  = GetCharacterWorldPosition(frame.joltHandle);
    bossAction.leapTakeoffPos = takeoffPos;

    // 跳び方の決め方で着地点を出す
    switch (bossAction.leapDef.landing)
    {
    case BossLeapLanding::AwayToCenter:
        bossAction.leapLandingPos = AwayToCenterLandingPoint(bossAction, takeoffPos, frame.playerPos, frame.battle);
        break;
    case BossLeapLanding::TowardTarget:
        bossAction.leapLandingPos = TowardTargetLandingPoint(bossAction, takeoffPos, frame.playerPos);
        break;
    case BossLeapLanding::InPlace:
        bossAction.leapLandingPos = takeoffPos; // 上がって同じ所へ降りる（滞空）
        break;
    }

    // 着地点をステージの内側へ収める
    // 壁の外だと空中で壁を押し続け、着地の時刻と攻撃判定がずれる
    bossAction.leapLandingPos = ClampInsideArena(bossAction.leapLandingPos, frame.battle);

    // 飛ぶ向き（体を向ける向き）を、踏み切り→着地点で固定する
    Vector3 flightDir = FlatToward(takeoffPos, bossAction.leapLandingPos);
    if (flightDir.LengthSquared() > 0.0001f)
    {
        flightDir.Normalize();
        bossAction.lockedDirection = flightDir;
    }

    // 頂点の高さを決める
    // 踏み切った瞬間の相手との距離で、近いほど低く、遠いほど高くする
    const float distanceToPlayer = FlatToward(takeoffPos, frame.playerPos).Length();
    const float apexHeight       = LeapApexHeight(bossAction.leapDef.height, distanceToPlayer);

    // 飛ぶ移動を予約する
    // 滞空は頂点まで速く上がって止まり、ほかは着地点まで放物線で飛ぶ
    const float flightTime = (std::max)(0.05f, bossAction.leapDef.flight);
    if (bossAction.leapDef.hover > 0.0f)
    {
        Vector3 apexPos = Vector3::Lerp(takeoffPos, bossAction.leapLandingPos, 0.5f);
        apexPos.y += apexHeight;
        frame.movement.MoveTo(apexPos, flightTime * 0.5f, {.ease = MoveEase::EaseOut});
    }
    else
    {
        frame.movement.MoveTo(bossAction.leapLandingPos, flightTime, {.height = apexHeight});
    }
    bossAction.leapPhase = BossLeapPhase::Flight;
}



// 跳躍の頂点の高さを返す
// 基準の高さに、相手が近いほど低く・遠いほど高くなる分を足す
float BossActionSystem::LeapApexHeight(float baseHeight, float distanceToPlayer) const
{
    // 近い〜遠いの間のどこにいるかを 0〜1 で出し、足す高さを直線で決める
    const float distanceRange = kLeapHeightFarDistance - kLeapHeightNearDistance;
    const float farRatio      = std::clamp((distanceToPlayer - kLeapHeightNearDistance) / distanceRange, 0.0f, 1.0f);
    const float heightOffset  = kLeapHeightOffsetMin + (kLeapHeightOffsetMax - kLeapHeightOffsetMin) * farRatio;

    // 基準が低い跳躍でも、地面より下を通らないようにする
    return (std::max)(0.0f, baseHeight + heightOffset);
}



// 予約した移動が終わったときに、跳躍の次の段へ進む
// 滞空だけは 上がる → 頂点に留まる → 降りる の3段。ほかは着地する
void BossActionSystem::AdvanceLeapPhase(BossActionComponent &bossAction, const Frame &frame) const
{
    const float halfFlightTime = (std::max)(0.05f, bossAction.leapDef.flight) * 0.5f;

    if (bossAction.leapPhase == BossLeapPhase::Flight && bossAction.leapDef.hover > 0.0f)
    {
        // 頂点に着いた。その場に留まる
        frame.movement.Hold(bossAction.leapDef.hover);
        bossAction.leapPhase = BossLeapPhase::Hover;
    }
    else if (bossAction.leapPhase == BossLeapPhase::Hover)
    {
        // 留まり終えた。ゆっくり降り始めて、着地点で一番速くなる
        frame.movement.MoveTo(bossAction.leapLandingPos, halfFlightTime, {.ease = MoveEase::EaseIn});
        bossAction.leapPhase = BossLeapPhase::Descent;
    }
    else
    {
        // 着地した。速度は予約が 0 にして止める
        bossAction.leapPhase = BossLeapPhase::Recovery;
    }
}



// 跳躍の空中。跳んでいる向きへ体を向ける
// 滞空だけは進まないので、浮いたまま相手を向き続ける
void BossActionSystem::ProcessLeapAir(BossActionComponent &bossAction, const Frame &frame)
{
    Vector3 lookDir = bossAction.lockedDirection;
    if (bossAction.leapDef.landing == BossLeapLanding::InPlace)
    {
        lookDir = FlatToward(frame.transform.position, frame.playerPos);
    }
    FaceToward(frame.transform, lookDir, bossAction.turnSpeed, frame.dt, frame.joltHandle);

    bossAction.moveDirection = bossAction.lockedDirection;
}



// 跳躍の着地硬直。硬直を数え、跳躍攻撃でなければ相手へゆっくり向き直る
void BossActionSystem::ProcessLeapLanding(BossActionComponent &bossAction, const Frame &frame)
{
    ProcessRecovery(bossAction, bossAction.leapDef.landRecovery, frame);

    // 跳躍攻撃の硬直中は向きを固定する
    // 相手を追うと、横へ避けても判定が付いてきて後隙を攻撃できない
    if (bossAction.leapDef.landAttack) return;

    FaceToward(frame.transform, FlatToward(frame.transform.position, frame.playerPos), bossAction.idleTurnSpeed, frame.dt, frame.joltHandle);
}



// ボスの突進攻撃
// 下がる → 空中で狙う → 構える → 突っ込む → 走り続ける → 止まる → 硬直
// 各段の移動は予約が運び、ここでは段の切り替えと向きだけを決める
void BossActionSystem::ProcessDash(BossActionComponent &bossAction, const Frame &frame)
{
    // --- 段の切り替え ---

    // 予約した移動が終わったら、次の段の移動を予約する
    // 下がる移動は、入った瞬間に OnEnterState が予約済み
    if (!frame.movement.IsMoving())
    {
        switch (bossAction.dashPhase)
        {
        case BossDashPhase::Retreat:
            // 空中で止まり、相手を向いて狙う
            frame.movement.MoveTo(bossAction.dashAimPos, (std::max)(0.0f, bossAction.dashAimTime));
            bossAction.dashPhase = BossDashPhase::Aim;
            break;

        case BossDashPhase::Aim:
            // 構えに入った瞬間に、突っ込む方向と終点を固定する
            // 以後は相手が動いても向きを変えない
            LockDashAim(bossAction, frame.playerPos, frame.battle);
            frame.movement.MoveTo(bossAction.dashAimPos, (std::max)(0.0f, bossAction.dashWindupTime));
            bossAction.dashPhase = BossDashPhase::Windup;
            break;

        case BossDashPhase::Windup: {
            // 空中から地面の終点へ、等速でまっすぐ突っ込む
            // 突進を速くしても頭打ちにならないよう、突進の速さの2倍までは許す
            const float chargeMaxSpeed = (std::max)(kMaxTrackSpeed, bossAction.dashSpeed * 2.0f);
            frame.movement.MoveTo(bossAction.dashEndPos, bossAction.dashChargeTime, {.maxSpeed = chargeMaxSpeed});
            bossAction.dashPhase = BossDashPhase::Charge;
            break;
        }

        case BossDashPhase::Charge:
            // 着地後も同じ向きへ走り続ける（壁に当たれば沿って滑る）
            frame.movement.MoveInDirection(bossAction.lockedDirection, bossAction.dashTravelSpeed,
                                       bossAction.dashTravelTime, bossAction.dashTravelSpeed);
            bossAction.dashPhase = BossDashPhase::Travel;
            break;

        case BossDashPhase::Travel:
            // 走る速さから 0 へ、直線で落として止まる
            frame.movement.MoveInDirection(bossAction.lockedDirection, bossAction.dashTravelSpeed,
                                       (std::max)(0.0f, bossAction.dashStopTime), 0.0f);
            bossAction.dashPhase = BossDashPhase::Stop;
            break;

        default:
            // 止まった。速度は予約が 0 にして止める
            bossAction.dashPhase = BossDashPhase::Recovery;
            break;
        }
    }

    // --- 硬直 ---

    // 足を止め、向きは突進の向きのまま固定する
    // 振り向くと、避けたのに狙われ続けているように見える
    if (bossAction.dashPhase == BossDashPhase::Recovery)
    {
        ProcessRecovery(bossAction, bossAction.dashRecovery, frame);
        return;
    }

    // --- 向き ---

    // 構えた後は、固定した方向を向く
    // ここで相手を追うと「構えた＝方向が決まった」と読めない
    Vector3 lookDir = bossAction.lockedDirection;

    // 下がる・狙う間は、相手を向き続ける
    // 構えに入った瞬間のこの向きが、突進の方向になる
    if (bossAction.dashPhase == BossDashPhase::Retreat || bossAction.dashPhase == BossDashPhase::Aim)
    {
        lookDir = FlatToward(GetCharacterWorldPosition(frame.joltHandle), frame.playerPos);
    }
    FaceToward(frame.transform, lookDir, bossAction.turnSpeed, frame.dt, frame.joltHandle);

    bossAction.moveDirection = bossAction.lockedDirection;
}



// ボスの様子見。相手を向いたまま、間合いを保つ向きへ動き回る
// 向きを毎フレーム決め直し、速度の上げ下げは物理（Jolt の加速・減速）に任せるので、曲がりながら滑らかに動く
void BossActionSystem::ProcessFootwork(BossActionComponent &bossAction, const Frame &frame)
{
    const Vector3 bossPos     = GetCharacterWorldPosition(frame.joltHandle);
    Vector3       toPlayerDir = FlatToward(bossPos, frame.playerPos);

    // 相手と重なっていて向きが決まらないときは止まる
    if (toPlayerDir.LengthSquared() <= 0.0001f)
    {
        frame.movement.desiredVelocity = Vector3::Zero;
        return;
    }
    toPlayerDir.Normalize();

    // --- 動く向き ---

    // 間合いを保つ向きを、今の位置と相手の位置から毎フレーム出し直す
    Vector3 keepDistance = KeepDistanceVector(bossPos, toPlayerDir, bossAction.footworkLeft, bossAction, frame);

    // 壁に押し返されて、回っている側へ進めなくなったら、反対側へ切り返す
    // 切り返した後は壁の押す向きと回る向きがそろうので、すぐには戻らない
    const Vector3 rightDir(toPlayerDir.z, 0.0f, -toPlayerDir.x);
    Vector3       sideDir = rightDir;
    if (bossAction.footworkLeft) sideDir = -rightDir;
    if (keepDistance.Dot(sideDir) < 0.0f)
    {
        bossAction.footworkLeft = !bossAction.footworkLeft;
        keepDistance            = KeepDistanceVector(bossPos, toPlayerDir, bossAction.footworkLeft, bossAction, frame);
    }

    Vector3 moveDir = keepDistance;
    if (moveDir.LengthSquared() > 0.0001f) moveDir.Normalize();

    // 向きと速さを渡す。速さを急に変えても、物理が加速・減速を付ける
    frame.movement.desiredVelocity = moveDir;
    frame.movement.overrideSpeed   = bossAction.footworkSpeed;
    bossAction.moveDirection       = moveDir;

    // --- 体の向き ---

    // 体は相手を向き続ける。動く向きとのずれは、アニメの前後左右で見せる
    FaceToward(frame.transform, toPlayerDir, bossAction.turnSpeed, frame.dt, frame.joltHandle);
    bossAction.footworkAnimDir = FootworkAnimDir(moveDir, frame.transform, bossAction.footworkAnimDir);

    // --- 終わり ---

    // 決めた長さが来たら、BT へ終わりを知らせる。止まらずに動き続ける
    // 次も様子見なら切れ目なくつながり、技なら待機に入って物理の減速で止まる
    if (!bossAction.actionFinished && bossAction.actionTimer >= bossAction.footworkTime)
    {
        bossAction.actionFinished = true;
        bossAction.lastActionTime = bossAction.actionTimer;
    }
}



// 技の硬直。足を止めて硬直の時間を数え、終わったら BT へ技の終わりを知らせる
void BossActionSystem::ProcessRecovery(BossActionComponent &bossAction, float recoveryTime, const Frame &frame)
{
    frame.movement.desiredVelocity = Vector3::Zero;

    bossAction.recoveryTimer += frame.dt;
    if (bossAction.actionFinished || bossAction.recoveryTimer < recoveryTime) return;

    // BT はこれを見て次の行動へ進む
    // 長さを残すのは、インスペクタで技の長さを確かめるため
    bossAction.actionFinished = true;
    bossAction.lastActionTime = bossAction.actionTimer;
}



// ---------------------------------------------------------------------------
// 技の補助（狙い・着地点・横移動の計画）
// ---------------------------------------------------------------------------

// 突進の方向を固定し、突っ込む終点と長さを決める
// 構え（Windup）に入る瞬間に1回だけ呼ぶ
void BossActionSystem::LockDashAim(BossActionComponent &bossAction, const Vector3 &playerPos,
                                   const BattleContext &battle) const
{
    // 狙うのは相手への「方向」だけ
    // 相手の位置で止めず、走り抜けた先で間合いを離す
    const Vector3 aimToPlayer = FlatToward(bossAction.dashAimPos, playerPos);

    // 真下に相手が居て向きが決まらないときは、下がる前の向きで突っ込む
    const float aimToPlayerDistance = aimToPlayer.Length();
    Vector3     chargeDir           = bossAction.lockedDirection;
    if (aimToPlayerDistance > 0.001f) chargeDir = aimToPlayer / aimToPlayerDistance;

    // 終点は構えた向きへまっすぐ dashDistance 先の地面（平らなステージ前提）
    // 空中から地面へ下りながら進む。壁際では向きを変えず、手前で止める
    Vector3 chargeEndPos = StraightPointInArena(bossAction.dashAimPos, chargeDir, bossAction.dashDistance, battle);
    chargeEndPos.y       = bossAction.dashStartPos.y;

    bossAction.dashEndPos      = chargeEndPos;
    bossAction.lockedDirection = chargeDir;

    // 突っ込む長さは、実際に走る距離（高さを含む）÷速さ
    const float chargeDistance = Vector3::Distance(bossAction.dashAimPos, bossAction.dashEndPos);
    bossAction.dashChargeTime  = chargeDistance / (std::max)(1.0f, bossAction.dashSpeed);
}



// 横移動の左右を技から決め、その真横へ動き出す
// 技に入る瞬間に1回だけ呼ぶ
void BossActionSystem::BeginSideTravel(ActionID actionId, float travelDistance, BossActionComponent &bossAction,
                                       const Frame &frame) const
{
    // lockedDirection は相手への向き。その真横を左右で選ぶ
    // 前 (fx,0,fz) の右は (fz,0,-fx)
    const Vector3 toPlayerDir = bossAction.lockedDirection;
    const Vector3 rightDir(toPlayerDir.z, 0.0f, -toPlayerDir.x);

    Vector3 moveDir = rightDir;
    if (actionId == A_BossSideTravelLeft) moveDir = -rightDir;

    BeginTravel(moveDir, travelDistance, bossAction, frame);
}



// 間合いを取る移動の向きを決める
// 横 ＋ 距離を保つ前後 ＋ 壁から離れる内向き を足し、前後への傾きを抑える
Vector3 BossActionSystem::RepositionDirection(float travelDistance, const BossActionComponent &bossAction,
                                              const Frame &frame) const
{
    // lockedDirection は相手への向き（OnEnterState が入れた）
    // 前 (fx,0,fz) の右は (fz,0,-fx)
    const Vector3 toPlayerDir = bossAction.lockedDirection;
    const Vector3 rightDir(toPlayerDir.z, 0.0f, -toPlayerDir.x);
    const Vector3 bossPos = GetCharacterWorldPosition(frame.joltHandle);

    // --- 横 ---

    // 左右それぞれ、壁の手前までに実際に動ける距離
    const Vector3 rightEnd          = StraightPointInArena(bossPos, rightDir, travelDistance, frame.battle);
    const Vector3 leftEnd           = StraightPointInArena(bossPos, -rightDir, travelDistance, frame.battle);
    const float   rightMoveDistance = FlatToward(bossPos, rightEnd).Length();
    const float   leftMoveDistance  = FlatToward(bossPos, leftEnd).Length();

    // 動ける距離が長い側へ回る
    // どちらも同じなら前回と同じ側。相手の周りを回り続ける
    bool moveLeft = bossAction.sideTravelLeft;
    if (leftMoveDistance > rightMoveDistance + 0.1f) moveLeft = true;
    if (rightMoveDistance > leftMoveDistance + 0.1f) moveLeft = false;

    // --- 合成 ---

    // 横 ＋ 距離を保つ前後 ＋ 壁から離れる内向き を足し、横の成分と前後の成分に分け直す
    // 前後の成分は + が相手へ近づく向き
    const Vector3 combined    = KeepDistanceVector(bossPos, toPlayerDir, moveLeft, bossAction, frame);
    float         sideAmount  = combined.Dot(rightDir);
    float         frontAmount = combined.Dot(toPlayerDir);

    // 横がほぼ消えたら、選んだ側へ戻す
    // 真前・真後ろへの移動は、左右の横移動のアニメで見せられない
    if (std::fabs(sideAmount) < 0.01f)
    {
        sideAmount = 0.01f;
        if (moveLeft) sideAmount = -0.01f;
    }

    // 前後は、真横から kRepositionMaxTiltDeg 度の傾きまでに抑える
    const float maxFrontAmount = std::fabs(sideAmount) * std::tan(DirectX::XMConvertToRadians(kRepositionMaxTiltDeg));
    frontAmount                = std::clamp(frontAmount, -maxFrontAmount, maxFrontAmount);

    Vector3 moveDir = rightDir * sideAmount + toPlayerDir * frontAmount;
    moveDir.Normalize();
    return moveDir;
}



// 間合いを保つ向きを返す（正規化しない）。横 ＋ 距離を保つ前後 ＋ 壁から離れる内向き を足したもの
// 間合いを取る（向きを1回だけ決める）と様子見（毎フレーム決め直す）の両方が使う
Vector3 BossActionSystem::KeepDistanceVector(const Vector3 &bossPos, const Vector3 &toPlayerDir, bool moveLeft,
                                             const BossActionComponent &bossAction, const Frame &frame) const
{
    // --- 横の向き ---

    // 前 (fx,0,fz) の右は (fz,0,-fx)
    const Vector3 rightDir(toPlayerDir.z, 0.0f, -toPlayerDir.x);
    Vector3       sideDir = rightDir;
    if (moveLeft) sideDir = -rightDir;

    // --- 距離を保つ前後 ---

    // 近すぎれば離れ、遠すぎれば詰める。+ が詰める、- が離れる
    // 保ちたい距離から外れているほど強くし、kRepositionRadialRange で横と同じ強さになる
    const float distanceToPlayer = FlatToward(bossPos, frame.playerPos).Length();

    // 距離を保つ前後の強さ。-1〜+1 で、0 がちょうど良い距離
    float       approachWeight   = 0.0f;

    // 保ちたい距離より近いときは、近いほど強く離れる
    if (distanceToPlayer < bossAction.repositionNearDistance)
    {
        approachWeight = -(bossAction.repositionNearDistance - distanceToPlayer) / kRepositionRadialRange;
    }
    // 保ちたい距離より遠いときは、遠いほど強く詰める
    if (distanceToPlayer > bossAction.repositionFarDistance)
    {
        approachWeight = (distanceToPlayer - bossAction.repositionFarDistance) / kRepositionRadialRange;
    }
    approachWeight = std::clamp(approachWeight, -1.0f, 1.0f);

    // --- 壁から離れる内向き ---

    // 壁に近いほど強く、ステージの中心へ寄る。長さが強さ（0〜1）
    // 壁際で真横へ回ると、壁に沿って動けなくなるため
    Vector3       inwardPush        = Vector3::Zero;
    const Vector3 bossToCenter      = FlatToward(bossPos, Vector3(frame.battle.arenaCenter));
    const float   distanceToWall    = frame.battle.arenaRadius - bossToCenter.Length();
    const float   wallAvoidDistance = bossAction.repositionWallDistance;

    // 壁の手前に居るときだけ、中心へ押す
    if (wallAvoidDistance > 0.0f && distanceToWall < wallAvoidDistance && bossToCenter.LengthSquared() > 0.0001f)
    {
        const float pushStrength = std::clamp((wallAvoidDistance - distanceToWall) / wallAvoidDistance, 0.0f, 1.0f);
        inwardPush               = bossToCenter / bossToCenter.Length() * pushStrength;
    }

    return sideDir + toPlayerDir * approachWeight + inwardPush;
}



// 様子見の準備。1回の長さを下限〜上限から引き、相手の周りを回る側を決める
// 技に入る瞬間に1回だけ呼ぶ
void BossActionSystem::BeginFootwork(BossActionComponent &bossAction, const Frame &frame)
{
    std::uniform_real_distribution<float> random01(0.0f, 1.0f);

    // --- 長さ ---

    // 長さを毎回変えて、次の技までのリズムを読まれにくくする
    const float minTime     = (std::max)(0.0f, bossAction.footworkMinTime);
    const float maxTime     = (std::max)(minTime, bossAction.footworkMaxTime);
    bossAction.footworkTime = minTime + (maxTime - minTime) * random01(m_random);

    // 前の様子見の向きを持ち越さない（切り替えの判定が前回の向きから始まる）
    bossAction.footworkAnimDir = BossFootworkDir::None;

    // --- 回る側 ---

    // lockedDirection は相手への向き（OnEnterState が入れた）
    // 前 (fx,0,fz) の右は (fz,0,-fx)
    const Vector3 toPlayerDir = bossAction.lockedDirection;
    const Vector3 rightDir(toPlayerDir.z, 0.0f, -toPlayerDir.x);
    const Vector3 bossPos = GetCharacterWorldPosition(frame.joltHandle);

    // 左右それぞれ、壁の手前までに動ける距離（kFootworkSideLookAhead まで）
    const Vector3 rightEnd          = StraightPointInArena(bossPos, rightDir, kFootworkSideLookAhead, frame.battle);
    const Vector3 leftEnd           = StraightPointInArena(bossPos, -rightDir, kFootworkSideLookAhead, frame.battle);
    const float   rightMoveDistance = FlatToward(bossPos, rightEnd).Length();
    const float   leftMoveDistance  = FlatToward(bossPos, leftEnd).Length();

    // 空きが広い側へ回る。どちらも空いていればランダム
    // 毎回同じ側へ回ると、相手に読まれる
    bossAction.footworkLeft = (random01(m_random) < 0.5f);
    if (leftMoveDistance > rightMoveDistance + 0.1f) bossAction.footworkLeft = true;
    if (rightMoveDistance > leftMoveDistance + 0.1f) bossAction.footworkLeft = false;
}



// 様子見のアニメの向き（前後左右）を、体の正面から見た動く向きで選ぶ
// 一番近い向きが、今の向きより kFootworkAnimDirHysteresis 以上近いときだけ切り替える
BossFootworkDir BossActionSystem::FootworkAnimDir(const Vector3 &moveDir, const TransformComponent &transform,
                                                  BossFootworkDir currentDir) const
{
    // 体の正面と右（水平）
    Vector3 facingDir = Vector3::Transform(Vector3::UnitZ, transform.rotation);
    facingDir.y       = 0.0f;
    if (facingDir.LengthSquared() > 0.0001f) facingDir.Normalize();
    const Vector3 facingRightDir(facingDir.z, 0.0f, -facingDir.x);

    // 動く向きが、前と右へそれぞれどれだけ向いているか（-1〜1）
    const float frontAmount = moveDir.Dot(facingDir);
    const float rightAmount = moveDir.Dot(facingRightDir);

    // 4方向それぞれの近さ。様子見していない（None）は一番遠い扱い
    auto closenessOf = [&](BossFootworkDir dir) {
        switch (dir)
        {
        case BossFootworkDir::Forward: return frontAmount;
        case BossFootworkDir::Back:    return -frontAmount;
        case BossFootworkDir::Left:    return -rightAmount;
        case BossFootworkDir::Right:   return rightAmount;
        default:                       return -1.0f;
        }
    };

    BossFootworkDir closestDir = BossFootworkDir::Forward;
    for (BossFootworkDir dir : {BossFootworkDir::Back, BossFootworkDir::Left, BossFootworkDir::Right})
    {
        if (closenessOf(dir) > closenessOf(closestDir)) closestDir = dir;
    }

    if (closenessOf(closestDir) > closenessOf(currentDir) + kFootworkAnimDirHysteresis) return closestDir;
    return currentDir;
}



// 横移動の動き方（加速 → 最高速 → 減速）で、好きな向きへ動き出す
// 向きは水平で長さ 1。真横でなくてもよい（間合いを取る移動が斜めに使う）
void BossActionSystem::BeginTravel(const Vector3 &moveDir, float travelDistance, BossActionComponent &bossAction,
                                   const Frame &frame) const
{
    // --- アニメの左右 ---

    // アニメは左右の2つしか無いので、相手から見て左寄りか右寄りかで選ぶ
    // lockedDirection はまだ相手への向き（下で進む向きに書き換える）
    const Vector3 toPlayerDir = bossAction.lockedDirection;
    const Vector3 rightDir(toPlayerDir.z, 0.0f, -toPlayerDir.x);
    bossAction.sideTravelLeft  = (moveDir.Dot(rightDir) < 0.0f);
    bossAction.sideTravelPhase = BossSideTravelPhase::Start;

    // --- 進む向きと距離 ---

    // 以後は進む向きを変えない。相手が動いても追わない
    bossAction.lockedDirection = moveDir;

    // 壁の手前で止まるよう、実際に動く距離に縮める
    const Vector3 bossPos      = GetCharacterWorldPosition(frame.joltHandle);
    const Vector3 travelEnd    = StraightPointInArena(bossPos, bossAction.lockedDirection, travelDistance, frame.battle);
    const float   moveDistance = FlatToward(bossPos, travelEnd).Length();

    // --- 段の長さ ---

    // 速さは直線で上げ下げするので、加速と減速の間に進むのは 最高速×時間÷2。残りを最高速で進む
    // 距離が足りなければ最高速は落とさず、加速と減速を同じ割合で縮める（遅く動くより、早く止まる）
    const float maxSpeed     = (std::max)(1.0f, bossAction.sideTravelSpeed);
    float       accelTime    = (std::max)(0.0f, bossAction.sideTravelAccelTime);
    float       decelTime    = (std::max)(0.0f, bossAction.sideTravelDecelTime);
    const float rampDistance = maxSpeed * (accelTime + decelTime) * 0.5f;
    float       cruiseTime   = 0.0f;
    if (moveDistance >= rampDistance)
    {
        cruiseTime = (moveDistance - rampDistance) / maxSpeed;
    }
    else
    {
        const float shrinkRatio = moveDistance / rampDistance;
        accelTime *= shrinkRatio;
        decelTime *= shrinkRatio;
    }
    bossAction.sideTravelCruiseTime = cruiseTime;
    bossAction.sideTravelStopTime   = decelTime;

    // 0 から最高速へ加速する
    frame.movement.MoveInDirection(bossAction.lockedDirection, 0.0f, accelTime, maxSpeed);
}



// 中央を挟んで、相手と反対側の着地点を返す（中央へ逃げる跳躍）
// 中心からの距離は 半径×escapeLandingRatio
Vector3 BossActionSystem::AwayToCenterLandingPoint(const BossActionComponent &bossAction, const Vector3 &takeoffPos,
                                                   const Vector3 &playerPos, const BattleContext &battle) const
{
    // 高さは踏み切りと同じにする（平らなステージ前提）
    Vector3 arenaCenterPos(battle.arenaCenter);
    arenaCenterPos.y = takeoffPos.y;

    // 相手→中央の向きへ抜ける
    // 相手から見ると、頭上を越えて距離を取られる形になる
    Vector3 awayDir = FlatToward(playerPos, arenaCenterPos);

    // 相手がちょうど中央に居るときは、自分→中央で代用する
    if (awayDir.LengthSquared() <= 0.0001f)
    {
        awayDir = FlatToward(takeoffPos, arenaCenterPos);
    }
    if (awayDir.LengthSquared() <= 0.0001f) return arenaCenterPos; // 全員が中央に重なっている
    awayDir.Normalize();

    // 割合は 0.9 で頭打ちにする
    // 1 に近いと反対側の端に降り、今度はそちらで追い込まれる
    const float landingRatio = (std::max)(0.0f, (std::min)(bossAction.escapeLandingRatio, 0.9f));
    return arenaCenterPos + awayDir * (battle.arenaRadius * landingRatio);
}



// 相手の attackStandOff だけ手前の着地点を返す（跳躍攻撃）
Vector3 BossActionSystem::TowardTargetLandingPoint(const BossActionComponent &bossAction, const Vector3 &takeoffPos,
                                                   const Vector3 &playerPos) const
{
    const Vector3 takeoffToPlayer = FlatToward(takeoffPos, playerPos);

    const float distanceToPlayer = takeoffToPlayer.Length();
    if (distanceToPlayer <= 0.001f) return takeoffPos; // 真上に居る。その場で跳ぶ

    // 手前で止めるぶんだけ引く。近すぎれば 0 ＝その場で跳んで叩く
    const float landingDistance = (std::max)(0.0f, distanceToPlayer - bossAction.attackStandOff);
    return takeoffPos + takeoffToPlayer * (landingDistance / distanceToPlayer);
}



// 点がステージの円の外なら、中心へ向かって円の縁まで引き戻す
Vector3 BossActionSystem::ClampInsideArena(const Vector3 &point, const BattleContext &battle) const
{
    const Vector3 arenaCenterPos(battle.arenaCenter);
    const Vector3 centerToPoint = FlatToward(arenaCenterPos, point);

    const float innerRadius        = (std::max)(0.0f, battle.arenaRadius - kWallMargin);
    const float distanceFromCenter = centerToPoint.Length();
    if (distanceFromCenter <= innerRadius) return point;

    Vector3 clampedPos = arenaCenterPos + centerToPoint * (innerRadius / distanceFromCenter);
    clampedPos.y       = point.y; // 高さは着地点の決め方に任せる
    return clampedPos;
}



// moveDir の向きへまっすぐ moveDistance 進んだ地点を返す（横移動・後方ステップ・突進の下がる・突っ込む）
// 壁際でも向きは変えず、距離だけ縮めて壁の手前で止める
Vector3 BossActionSystem::StraightPointInArena(const Vector3 &startPos, const Vector3 &moveDir, float moveDistance,
                                               const BattleContext &battle) const
{
    // 中心から見た今の位置（水平）
    const Vector3 centerToStart = FlatToward(Vector3(battle.arenaCenter), startPos);
    const float   innerRadius   = (std::max)(0.0f, battle.arenaRadius - kWallMargin);

    // まっすぐ進んで内側の円の縁に着くまでの距離を、|centerToStart + moveDir × distanceToEdge| = innerRadius から求める
    // 縁の外から外向きに進むときは 0（その場で止まる）
    const float startAlongMove = centerToStart.Dot(moveDir);
    const float discriminant   = startAlongMove * startAlongMove - (centerToStart.LengthSquared() - innerRadius * innerRadius);
    float       distanceToEdge = 0.0f;
    if (discriminant > 0.0f) distanceToEdge = (std::max)(0.0f, -startAlongMove + std::sqrt(discriminant));

    return startPos + moveDir * (std::min)((std::max)(0.0f, moveDistance), distanceToEdge);
}



// BehaviorTreeSystem(L02) の後、JoltCharacterUpdateSystem(L03) の前。
REGISTER_LOGIC_SYSTEM(BossActionSystem, Priority::LogicStage::L02_PostUpdate);
