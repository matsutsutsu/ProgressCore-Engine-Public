#include "BossActionSystem.h"

#include "Game/Logics/AI/Battle/BattleContext.h"
#include "Game/Logics/AI/BehaviorTree/Data/BTActionIds.h"
#include "Engine/Core/App/GameFlowState.h"                           // cinematic
#include "Engine/Animation/Core/AnimStateMachine.h"                  // 止まりのモーションの長さ
#include "Engine/Animation/Components/AnimParametersComponent.h"     // 技の開始のトリガー
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
        Vector3 dir = to - from;
        dir.y       = 0.0f;
        return dir;
    }



    // 向きたい方向に、turnSpeed の速さで回転する
    void FaceToward(TransformComponent &trans, const Vector3 &lookDir, float turnSpeed, float dt,
                    const JoltCharacterHandleComponent &handle)
    {
        if (lookDir.LengthSquared() <= 0.001f) return;

        // 水平の向きだけで yaw を計算する
        float targetYaw = atan2f(lookDir.x, lookDir.z);

        // 今の yaw を計算する
        Vector3 currentForward = Vector3::Transform(Vector3::UnitZ, trans.rotation);
        float   currentYaw     = atan2f(currentForward.x, currentForward.z);

        // 近い回り方で yaw を変える
        float diff   = WrapAngle(targetYaw - currentYaw);
        float newYaw = currentYaw + diff * (std::min)(turnSpeed * dt, 1.0f);

        SetCharacterRotation(trans, handle, Quaternion::CreateFromAxisAngle(Vector3::UnitY, newYaw));
    }



    // 予約した移動の速さの上限の既定値 [m/s]（MoveToOptions::maxSpeed と同じ）
    // 突進はこれより速く突っ込むので、突進の速さに合わせて上げる
    constexpr float kMaxTrackSpeed = 150.0f;



    // ステージの壁から離す距離 [m]。カプセル半径（1.4m）に余裕を足した値
    // 着地点や下がる先が、この内側の円に収まるようにする
    constexpr float kWallMargin = 3.0f;



    // アニメのステートが1回流れ切る長さ（クリップの長さ÷再生速度）を返す
    // サブマシンの中も探す。見つからなければ fallback
    float StatePlayTime(const AnimStateGraph &graph, uint32_t stateHash, float fallback)
    {
        for (const AnimState &s : graph.states)
        {
            if (s.stateHash == stateHash && s.sequence && s.playbackSpeed > 0.0f)
                return s.sequence->duration / s.playbackSpeed;
            if (s.isSubMachine && s.subGraph)
            {
                const float t = StatePlayTime(*s.subGraph, stateHash, -1.0f);
                if (t >= 0.0f) return t;
            }
        }
        return fallback;
    }



    // 後方ステップの止まりのモーションを流すステート
    // （ValkyraAnimNode.json の BackStepStop）
    const uint32_t kBackStepStopState = CCL::Utils::HashString("BackStepStop");

    // 技の開始で引くアニメのトリガー（ValkyraAnimNode.json）
    // 跳躍3種は同じ "Leap"。着地のクリップは BossAnimSyncSystem が切り替える
    const uint32_t kBackStepTrigger = CCL::Utils::HashString("BackStep");
    const uint32_t kLeapTrigger     = CCL::Utils::HashString("Leap");
    const uint32_t kDashTrigger     = CCL::Utils::HashString("Dash");

    // 体の状態に入った瞬間に引くトリガーを返す。0 は引かない
    // 待機・移動・横歩きは、グラフが状態の値でクリップを選ぶ
    uint32_t StartTriggerOf(BossActionState state)
    {
        switch (state)
        {
        case BossActionState::BackStep: return kBackStepTrigger;
        case BossActionState::Leap:     return kLeapTrigger;
        case BossActionState::Dash:     return kDashTrigger;
        default:                        return 0;
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
    return types;
}



// ボス全員の、技の切り替えと1フレーム分の動き
void BossActionSystem::Update(float rawDt)
{
    // 処理フロー:
    // 1. このフレームにやるべき動きを決める（演出中なら待機）
    // 2. 動きが変わったフレームだけ、入ったときの処理とアニメの開始をする
    // 3. 今の動きを1フレーム分進める

    if (!m_world->HasResource<BattleContext>()) return;
    const auto &battle = m_world->GetResource<BattleContext>();
    if (!battle.HasPlayer()) return;

    const Vector3 playerPos(battle.playerPos); // プレイヤーの足元（ワールド）

    // 演出中・決着後かを、全個体ぶん1回だけ調べる
    // 決着後も止めるのは、倒れたプレイヤーへ技を出し続けないため
    const GameFlowState *flow      = m_world->HasResource<GameFlowState>() ? &m_world->GetResource<GameFlowState>() : nullptr;
    const bool           cinematic = flow && (flow->cinematic || flow->matchOver);

    ForEachWithID([&](EntityID                            entity,
                      CharacterMovementComponent         &movement,
                      BossActionComponent                &bossAction,
                      TransformComponent                 &trans,
                      const BossCommandComponent         &cmd,
                      const JoltCharacterConfigComponent &config,
                      const JoltCharacterHandleComponent &joltHandle,
                      const TimeStateComponent           &time) {
        if (!joltHandle.character) return;

        // この個体の時計の dt（ヒットストップ・スローを含む）
        const float dt = time.localDt;

        // ---------------------------------------------------------
        // 1. このフレームの動き
        // ---------------------------------------------------------

        // BT の要求を体の動きに変える
        // 演出中・決着後は、要求を読まずに待機にする
        BossActionState next;
        if (cinematic)
        {
            next = BossActionState::None;
        }
        else
        {
            // ActionIDの要求を、BossActionStateに変換する
            next = TranslateAction(cmd.currentActionId);
        }

        // このフレームのアクションに必要な情報をまとめる
        const Frame f{trans, movement, config, joltHandle, battle, playerPos, dt,
                      m_world->GetComponent<AnimStateMachineComponent>(entity)};

        // ---------------------------------------------------------
        // 2. 動きに入った瞬間
        // ---------------------------------------------------------

        // 状態が変わった瞬間に処理が走る
        // アニメーションやアクションのはじめだけの処理に使う
        // 依頼番号も見るのは、同じ技を続けて出したときに始め直すため
        if (next != bossAction.currentState || cmd.actionSerial != bossAction.actionSerial)
        {
            // 状態遷移時に１回だけ呼ばれる
            bossAction.actionSerial = cmd.actionSerial;
            OnEnterState(next, cmd.currentActionId, bossAction, f);

            // アクション状態に合わせたアニメのトリガーを引く
            const uint32_t trigger = StartTriggerOf(next);

            auto          *anim    = m_world->GetComponent<AnimParametersComponent>(entity);

            // アニメーションがあって、かつトリガーが設定されていればAnimationのトリガーを引く
            if (trigger != 0 && anim) anim->SetTrigger(trigger);
        }

        // 状態を更新して、経過時間を加算する
        bossAction.currentState = next;
        bossAction.actionTimer += dt;

        // ---------------------------------------------------------
        // 3. 1フレーム分進める
        // ---------------------------------------------------------

        switch (bossAction.currentState)
        {
        case BossActionState::Move:
            ProcessMove(bossAction, f);
            break;
        case BossActionState::Strafe:
            ProcessStrafe(bossAction, f);
            break;
        case BossActionState::BackStep:
            ProcessBackStep(bossAction, f);
            break;
        case BossActionState::Leap:
            ProcessLeap(bossAction, f);
            break;
        case BossActionState::Dash:
            ProcessDash(bossAction, f);
            break;
        default:
            ProcessIdle(bossAction, f);
            break;
        }
    });
}



// デバッグ用。ボスの位置をインスペクタから直接動かす
void BossActionSystem::OnGui()
{
    auto view = m_world->View<TransformComponent, BossActionComponent>();
    if (view.begin() == view.end())
    {
        ImGui::TextDisabled("Boss entity not found in scene.");
        return;
    }

    for (auto e : view)
    {
        auto *trans = m_world->GetComponent<TransformComponent>(e);
        if (!trans) continue;

        ImGui::PushID(static_cast<int>(e));
        ImGui::Text("Boss Entity ID: %llu", (unsigned long long)e);

        // 動かしたら瞬間移動として物理にも伝える
        float pos[3] = {trans->position.x, trans->position.y, trans->position.z};
        if (ImGui::DragFloat3("Boss Pos (Warp)", pos, 0.2f))
        {
            trans->position     = DirectX::SimpleMath::Vector3(pos[0], pos[1], pos[2]);
            trans->isTeleported = true;
            trans->isDirty      = true;
        }

        ImGui::PopID();
    }
}



// ---------------------------------------------------------------------------
// 移動系の状態
// ---------------------------------------------------------------------------

// ボスの待機。足を止め、相手の方へゆっくり向き直る
void BossActionSystem::ProcessIdle(BossActionComponent &bossAction, const Frame &f)
{
    // 足を止める
    // 止まり方の慣性は、物理側（Jolt の deceleration）が付ける
    f.movement.desiredVelocity = Vector3::Zero;

    // 相手の方へ、移動中より遅く向き直る
    // すぐ正対すると、背後を取る意味が無くなる
    FaceToward(f.trans, FlatToward(f.trans.position, f.playerPos), bossAction.idleTurnSpeed, f.dt,
               f.joltHandle);
}



// ボスの移動。Jolt の walkSpeed で相手へ向かって歩く
void BossActionSystem::ProcessMove(BossActionComponent &bossAction, const Frame &f)
{
    Vector3 dirToPlayer = FlatToward(f.trans.position, f.playerPos);

    // 相手と重なっていて向きが決まらないときは止まる
    if (dirToPlayer.LengthSquared() <= 0.001f)
    {
        f.movement.desiredVelocity = Vector3::Zero;
        return;
    }

    dirToPlayer.Normalize();

    // 相手の方へ歩く
    // overrideSpeed を書かないので、物理側が Jolt の walkSpeed で動かす
    f.movement.desiredVelocity = dirToPlayer;

    // dirToPlayer の方向へ、turnSpeed の速さで向き直る
    FaceToward(f.trans, dirToPlayer, bossAction.turnSpeed, f.dt, f.joltHandle);
}



// ボスの横歩き。相手を見たまま、相手を中心に回り込む
void BossActionSystem::ProcessStrafe(BossActionComponent &bossAction, const Frame &f)
{
    Vector3 toPlayer = FlatToward(f.trans.position, f.playerPos);

    // 相手と重なっていて向きが決まらないときは止まる
    const float distance = toPlayer.Length();
    if (distance <= 0.001f)
    {
        f.movement.desiredVelocity = Vector3::Zero;
        return;
    }
    toPlayer /= distance;

    // 相手への方向の直角へ進む
    // 前 (fx,0,fz) の右は (fz,0,-fx)。strafeSign が +1 で右回り、-1 で左回り
    Vector3 moveDir(toPlayer.z * bossAction.strafeSign, 0.0f, -toPlayer.x * bossAction.strafeSign);

    // 狙いの間合いからのずれを、半径方向に足す
    // 相手が詰めてきても、回りながら間合いを戻せる
    const float error  = (distance - bossAction.strafeKeepDistance) / (std::max)(1.0f, bossAction.strafeKeepDistance);
    const float radial = std::clamp(error * bossAction.strafeDistanceGain, -1.0f, 1.0f);
    moveDir += toPlayer * radial; // 正=遠いので近づく / 負=近いので離れる

    moveDir.Normalize();

    // 0.5秒先がステージの外なら、回る向きを1回だけ反転する
    // 毎フレーム反転すると、境界の上で足踏みになる
    if (!bossAction.strafeFlipped)
    {
        const Vector3 ahead = Vector3(f.trans.position) + moveDir * (bossAction.strafeSpeed * 0.5f);
        if ((ClampInsideArena(ahead, f.battle) - ahead).LengthSquared() > 0.01f)
        {
            bossAction.strafeSign    = -bossAction.strafeSign;
            bossAction.strafeFlipped = true;
            moveDir                  = Vector3(toPlayer.z * bossAction.strafeSign, 0.0f, -toPlayer.x * bossAction.strafeSign);
        }
    }

    bossAction.moveDirection   = moveDir;
    f.movement.desiredVelocity = moveDir;
    f.movement.overrideSpeed   = bossAction.strafeSpeed;

    // 進む向きではなく、相手を向き続ける
    // 横歩きのクリップ(Jog_Left/Right)が正しく見える前提
    FaceToward(f.trans, toPlayer, bossAction.strafeTurnSpeed, f.dt, f.joltHandle);
}



// ---------------------------------------------------------------------------
// 行動の切り替え
// ---------------------------------------------------------------------------

// BT の技(ActionID)を、体の動き(BossActionState)に変える
// 知らない技は待機(None)
BossActionState BossActionSystem::TranslateAction(ActionID actionId) const
{
    if (actionId == A_Move) return BossActionState::Move;
    if (actionId == A_BossStrafe) return BossActionState::Strafe;
    if (actionId == A_BossBackStep) return BossActionState::BackStep;
    if (actionId == A_BossLeapEscape) return BossActionState::Leap;
    if (actionId == A_BossLeapAttack) return BossActionState::Leap;
    if (actionId == A_BossAirHover) return BossActionState::Leap;
    if (actionId == A_BossDashAttack) return BossActionState::Dash;
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
void BossActionSystem::OnEnterState(BossActionState next, ActionID actionId, BossActionComponent &bossAction,
                                    const Frame &f) const
{
    bossAction.actionTimer    = 0.0f;
    bossAction.recoveryTimer  = 0.0f;
    bossAction.actionFinished = false;

    // 前の技の移動の予約を取り消す
    // 技が途中で切り替わっても、前の技の終点まで運ばれ続けない
    f.movement.CancelMove();

    // 進む向きを、相手への向きで固定する
    // 以後は相手が動いても追わないので、動く技を避けられる
    Vector3     toPlayer = FlatToward(f.trans.position, f.playerPos);
    const float distance = toPlayer.Length();
    if (distance > 0.001f)
    {
        toPlayer /= distance;
        bossAction.lockedDirection = toPlayer;
    }

    switch (next)
    {
    case BossActionState::BackStep: {
        // 後方ステップの準備。進む向きを相手と逆にする
        bossAction.lockedDirection = -bossAction.lockedDirection;
        bossAction.backStepPhase   = BossBackStepPhase::Retreat;

        // 止まりのモーションの長さを、アニメグラフから取る
        // 下がる長さ＝下がり始め＋止まりのモーション。取れなければ 0.4 秒
        bossAction.backStepStopTime = f.anim ? StatePlayTime(f.anim->internalGraph, kBackStepStopState, 0.4f) : 0.4f;

        const float retreatTime = (std::max)(0.0f, bossAction.backStepRetreatTime);
        const float moveTime    = (std::max)(0.05f, retreatTime + bossAction.backStepStopTime);

        // まっすぐ後ろの終点まで運ぶ予約をする
        // 速く下がり始めて終点で止まり、途中で少し浮いて地面に戻る
        const Vector3 now = GetCharacterWorldPosition(f.joltHandle);
        const Vector3 end = StraightPointInArena(now, bossAction.lockedDirection, bossAction.backStepDistance, f.battle);
        f.movement.MoveTo(end, moveTime, {.height = bossAction.backStepHeight, .ease = MoveEase::EaseOut});
        break;
    }

    case BossActionState::Strafe:
        // 横歩きの準備。前回と逆回りにする
        // 毎回同じ向きだと先が読める（壁際なら ProcessStrafe が取り直す）
        bossAction.strafeSign    = -bossAction.strafeSign;
        bossAction.strafeFlipped = false;
        break;

    case BossActionState::Leap:
        // 跳躍の準備。どの技かで跳び方（着地点の決め方・着地で攻撃するかを含む）を写す
        // 着地点そのものは、踏み切りの瞬間に TakeOffLeap が決める
        bossAction.leapPhase = BossLeapPhase::Windup;
        if (const BossLeapDef *def = LeapDefOf(actionId, bossAction))
        {
            bossAction.leapDef = *def;
        }
        break;

    case BossActionState::Dash: {
        // 突進の準備。段を最初に戻す
        // 突っ込む方向は、構えに入る瞬間に LockDashAim が決める
        bossAction.dashPhase = BossDashPhase::Retreat;

        // 下がって狙う地点（空中）を決める。まっすぐ後ろへ下がり、浮いたまま止まる
        // lockedDirection は相手への向きなので、その逆へ下がる
        const Vector3 now = GetCharacterWorldPosition(f.joltHandle);
        Vector3       aim = StraightPointInArena(now, -bossAction.lockedDirection, bossAction.dashRetreatDistance, f.battle);
        aim.y             = now.y + bossAction.dashRetreatHeight;

        bossAction.dashStartPos = now;
        bossAction.dashAimPos   = aim;

        // 狙う地点まで、速く下がり始めて止まる移動を予約する
        const float retreatTime = (std::max)(0.05f, bossAction.dashRetreatTime);
        f.movement.MoveTo(bossAction.dashAimPos, retreatTime, {.ease = MoveEase::EaseOut});
        break;
    }

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
void BossActionSystem::ProcessBackStep(BossActionComponent &bossAction, const Frame &f)
{
    // 入った瞬間の相手の方向を向き続ける（後から追わない）
    // 背を向けると、間合いを取り直す技ではなく逃げたように見える
    FaceToward(f.trans, -bossAction.lockedDirection, bossAction.turnSpeed, f.dt, f.joltHandle);

    // 下がり始め → 止まり の切り替えは、アニメのために時刻で決める
    const bool retreatOver = bossAction.actionTimer >= bossAction.backStepRetreatTime;
    if (bossAction.backStepPhase == BossBackStepPhase::Retreat && retreatOver)
        bossAction.backStepPhase = BossBackStepPhase::Stop;

    // 予約した移動が終わったら、着地硬直にする
    if (!f.movement.IsMoving()) bossAction.backStepPhase = BossBackStepPhase::Recovery;

    if (bossAction.backStepPhase == BossBackStepPhase::Recovery)
    {
        ProcessRecovery(bossAction, bossAction.backStepRecovery, f);
        return;
    }

    bossAction.moveDirection = bossAction.lockedDirection;
}



// ボスの跳躍。溜め → 放物線で飛ぶ → 着地硬直
// 跳躍3種はすべてここを通り、違いは leapDef（跳び方）だけ。飛ぶ移動は予約が運ぶ
void BossActionSystem::ProcessLeap(BossActionComponent &bossAction, const Frame &f)
{
    // 処理フロー:
    // 1. 溜め（時間が来たら踏み切る）
    // 2. 予約した移動が終わったら、次の段へ進む
    // 3. 空中なら向きを、着地した後なら硬直を進める

    if (bossAction.leapPhase == BossLeapPhase::Windup)
    {
        ProcessLeapWindup(bossAction, f);

        // まだ溜めている。踏み切ったフレームは、そのまま空中の処理へ進む
        if (bossAction.leapPhase == BossLeapPhase::Windup) return;
    }

    if (!f.movement.IsMoving()) AdvanceLeapPhase(bossAction, f);

    if (bossAction.leapPhase == BossLeapPhase::Recovery)
    {
        ProcessLeapLanding(bossAction, f);
    }
    else
    {
        ProcessLeapAir(bossAction, f);
    }
}



// 跳躍の溜め。足を止めて相手を向き、溜めの時間が来たら踏み切る
// 着地点はまだ決めない（溜めを見てから動いた相手に付いていかない）
void BossActionSystem::ProcessLeapWindup(BossActionComponent &bossAction, const Frame &f)
{
    // 溜め中は、相手の方を向き続ける
    f.movement.desiredVelocity = Vector3::Zero;
    FaceToward(f.trans, FlatToward(f.trans.position, f.playerPos), bossAction.turnSpeed, f.dt, f.joltHandle);

    // 溜めの時間が来るまで、飛ぶ移動の予約をしない
    if (bossAction.actionTimer < bossAction.leapDef.windup) return;

    TakeOffLeap(bossAction, f);
}



// 跳躍の踏み切り。着地点と飛ぶ向きを決め、最初の移動を予約する
// 滞空は「頂点へ上がる」だけを予約し、留まる・降りるは AdvanceLeapPhase が続ける
void BossActionSystem::TakeOffLeap(BossActionComponent &bossAction, const Frame &f) const
{
    // 踏み切った地点を覚える（跳躍中のドローンの陣形の高さに使う）
    const Vector3 takeoff     = GetCharacterWorldPosition(f.joltHandle);
    bossAction.leapTakeoffPos = takeoff;

    // 跳び方の決め方で着地点を出す
    switch (bossAction.leapDef.landing)
    {
    case BossLeapLanding::AwayToCenter:
        bossAction.leapLandingPos = AwayToCenterLandingPoint(bossAction, takeoff, f.playerPos, f.battle);
        break;
    case BossLeapLanding::TowardTarget:
        bossAction.leapLandingPos = TowardTargetLandingPoint(bossAction, takeoff, f.playerPos);
        break;
    case BossLeapLanding::InPlace:
        bossAction.leapLandingPos = takeoff; // 上がって同じ所へ降りる（滞空）
        break;
    }

    // 着地点をステージの内側へ収める
    // 壁の外だと空中で壁を押し続け、着地の時刻と攻撃判定がずれる
    bossAction.leapLandingPos = ClampInsideArena(bossAction.leapLandingPos, f.battle);

    // 飛ぶ向き（体を向ける向き）を、踏み切り→着地点で固定する
    Vector3 travel = FlatToward(takeoff, bossAction.leapLandingPos);
    if (travel.LengthSquared() > 0.0001f)
    {
        travel.Normalize();
        bossAction.lockedDirection = travel;
    }

    // 飛ぶ移動を予約する
    // 滞空は頂点まで速く上がって止まり、ほかは着地点まで放物線で飛ぶ
    const float flightTime = (std::max)(0.05f, bossAction.leapDef.flight);
    if (bossAction.leapDef.hover > 0.0f)
    {
        Vector3 apex = Vector3::Lerp(takeoff, bossAction.leapLandingPos, 0.5f);
        apex.y += bossAction.leapDef.height;
        f.movement.MoveTo(apex, flightTime * 0.5f, {.ease = MoveEase::EaseOut});
    }
    else
    {
        f.movement.MoveTo(bossAction.leapLandingPos, flightTime, {.height = bossAction.leapDef.height});
    }
    bossAction.leapPhase = BossLeapPhase::Flight;
}



// 予約した移動が終わったときに、跳躍の次の段へ進む
// 滞空だけは 上がる → 頂点に留まる → 降りる の3段。ほかは着地する
void BossActionSystem::AdvanceLeapPhase(BossActionComponent &bossAction, const Frame &f) const
{
    const float halfFlight = (std::max)(0.05f, bossAction.leapDef.flight) * 0.5f;

    if (bossAction.leapPhase == BossLeapPhase::Flight && bossAction.leapDef.hover > 0.0f)
    {
        // 頂点に着いた。その場に留まる
        f.movement.Hold(bossAction.leapDef.hover);
        bossAction.leapPhase = BossLeapPhase::Hover;
    }
    else if (bossAction.leapPhase == BossLeapPhase::Hover)
    {
        // 留まり終えた。ゆっくり降り始めて、着地点で一番速くなる
        f.movement.MoveTo(bossAction.leapLandingPos, halfFlight, {.ease = MoveEase::EaseIn});
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
void BossActionSystem::ProcessLeapAir(BossActionComponent &bossAction, const Frame &f)
{
    Vector3 lookDir = bossAction.lockedDirection;
    if (bossAction.leapDef.landing == BossLeapLanding::InPlace)
    {
        lookDir = FlatToward(f.trans.position, f.playerPos);
    }
    FaceToward(f.trans, lookDir, bossAction.turnSpeed, f.dt, f.joltHandle);

    bossAction.moveDirection = bossAction.lockedDirection;
}



// 跳躍の着地硬直。硬直を数え、跳躍攻撃でなければ相手へゆっくり向き直る
void BossActionSystem::ProcessLeapLanding(BossActionComponent &bossAction, const Frame &f)
{
    ProcessRecovery(bossAction, bossAction.leapDef.landRecovery, f);

    // 跳躍攻撃の硬直中は向きを固定する
    // 相手を追うと、横へ避けても判定が付いてきて後隙を攻撃できない
    if (bossAction.leapDef.landAttack) return;

    FaceToward(f.trans, FlatToward(f.trans.position, f.playerPos), bossAction.idleTurnSpeed, f.dt, f.joltHandle);
}



// ボスの突進攻撃
// 下がる → 空中で狙う → 構える → 突っ込む → 走り続ける → 止まる → 硬直
// 各段の移動は予約が運び、ここでは段の切り替えと向きだけを決める
void BossActionSystem::ProcessDash(BossActionComponent &bossAction, const Frame &f)
{
    // --- 段の切り替え ---

    // 予約した移動が終わったら、次の段の移動を予約する
    // 下がる移動は、入った瞬間に OnEnterState が予約済み
    if (!f.movement.IsMoving())
    {
        switch (bossAction.dashPhase)
        {
        case BossDashPhase::Retreat:
            // 空中で止まり、相手を向いて狙う
            f.movement.MoveTo(bossAction.dashAimPos, (std::max)(0.0f, bossAction.dashAimTime));
            bossAction.dashPhase = BossDashPhase::Aim;
            break;

        case BossDashPhase::Aim:
            // 構えに入った瞬間に、突っ込む方向と終点を固定する
            // 以後は相手が動いても向きを変えない
            LockDashAim(bossAction, f.playerPos, f.battle);
            f.movement.MoveTo(bossAction.dashAimPos, (std::max)(0.0f, bossAction.dashWindupTime));
            bossAction.dashPhase = BossDashPhase::Windup;
            break;

        case BossDashPhase::Windup: {
            // 空中から地面の終点へ、等速でまっすぐ突っ込む
            // 突進を速くしても頭打ちにならないよう、突進の速さの2倍までは許す
            const float maxSpeed = (std::max)(kMaxTrackSpeed, bossAction.dashSpeed * 2.0f);
            f.movement.MoveTo(bossAction.dashEndPos, bossAction.dashChargeTime, {.maxSpeed = maxSpeed});
            bossAction.dashPhase = BossDashPhase::Charge;
            break;
        }

        case BossDashPhase::Charge:
            // 着地後も同じ向きへ走り続ける（壁に当たれば沿って滑る）
            f.movement.MoveInDirection(bossAction.lockedDirection, bossAction.dashTravelSpeed,
                                       bossAction.dashTravelTime, bossAction.dashTravelSpeed);
            bossAction.dashPhase = BossDashPhase::Travel;
            break;

        case BossDashPhase::Travel:
            // 走る速さから 0 へ、直線で落として止まる
            f.movement.MoveInDirection(bossAction.lockedDirection, bossAction.dashTravelSpeed,
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
        ProcessRecovery(bossAction, bossAction.dashRecovery, f);
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
        lookDir = FlatToward(GetCharacterWorldPosition(f.joltHandle), f.playerPos);
    }
    FaceToward(f.trans, lookDir, bossAction.turnSpeed, f.dt, f.joltHandle);

    bossAction.moveDirection = bossAction.lockedDirection;
}



// 技の硬直。足を止めて硬直の時間を数え、終わったら BT へ技の終わりを知らせる
void BossActionSystem::ProcessRecovery(BossActionComponent &bossAction, float recoveryTime, const Frame &f)
{
    f.movement.desiredVelocity = Vector3::Zero;

    bossAction.recoveryTimer += f.dt;
    if (bossAction.actionFinished || bossAction.recoveryTimer < recoveryTime) return;

    // BT はこれを見て次の行動へ進む
    // 長さを残すのは、インスペクタで技の長さを確かめるため
    bossAction.actionFinished = true;
    bossAction.lastActionTime = bossAction.actionTimer;
}



// ---------------------------------------------------------------------------
// 技の補助（狙い・着地点）
// ---------------------------------------------------------------------------

// 突進の方向を固定し、突っ込む終点と長さを決める
// 構え（Windup）に入る瞬間に1回だけ呼ぶ
void BossActionSystem::LockDashAim(BossActionComponent &bossAction, const Vector3 &playerPos,
                                   const BattleContext &battle) const
{
    // 狙うのは相手への「方向」だけ
    // 相手の位置で止めず、走り抜けた先で間合いを離す
    const Vector3 toTarget = FlatToward(bossAction.dashAimPos, playerPos);

    // 真下に相手が居て向きが決まらないときは、下がる前の向きで突っ込む
    const float toTargetLen = toTarget.Length();
    Vector3     dir         = (toTargetLen > 0.001f) ? toTarget / toTargetLen : bossAction.lockedDirection;

    // 終点は構えた向きへまっすぐ dashDistance 先の地面（平らなステージ前提）
    // 空中から地面へ下りながら進む。壁際では向きを変えず、手前で止める
    Vector3 end = StraightPointInArena(bossAction.dashAimPos, dir, bossAction.dashDistance, battle);
    end.y       = bossAction.dashStartPos.y;

    bossAction.dashEndPos      = end;
    bossAction.lockedDirection = dir;

    // 突っ込む長さは、実際に走る距離（高さを含む）÷速さ
    const float length        = Vector3::Distance(bossAction.dashAimPos, bossAction.dashEndPos);
    bossAction.dashChargeTime = length / (std::max)(1.0f, bossAction.dashSpeed);
}



// 中央を挟んで、相手と反対側の着地点を返す（中央へ逃げる跳躍）
// 中心からの距離は 半径×escapeLandingRatio
Vector3 BossActionSystem::AwayToCenterLandingPoint(const BossActionComponent &bossAction, const Vector3 &takeoffPos,
                                                   const Vector3 &playerPos, const BattleContext &battle) const
{
    // 高さは踏み切りと同じにする（平らなステージ前提）
    Vector3 center(battle.arenaCenter);
    center.y = takeoffPos.y;

    // 相手→中央の向きへ抜ける
    // 相手から見ると、頭上を越えて距離を取られる形になる
    Vector3 away = FlatToward(playerPos, center);

    // 相手がちょうど中央に居るときは、自分→中央で代用する
    if (away.LengthSquared() <= 0.0001f)
    {
        away = FlatToward(takeoffPos, center);
    }
    if (away.LengthSquared() <= 0.0001f) return center; // 全員が中央に重なっている
    away.Normalize();

    // 割合は 0.9 で頭打ちにする
    // 1 に近いと反対側の端に降り、今度はそちらで追い込まれる
    const float ratio = (std::max)(0.0f, (std::min)(bossAction.escapeLandingRatio, 0.9f));
    return center + away * (battle.arenaRadius * ratio);
}



// 相手の attackStandOff だけ手前の着地点を返す（跳躍攻撃）
Vector3 BossActionSystem::TowardTargetLandingPoint(const BossActionComponent &bossAction, const Vector3 &takeoffPos,
                                                   const Vector3 &playerPos) const
{
    const Vector3 toTarget = FlatToward(takeoffPos, playerPos);

    const float distance = toTarget.Length();
    if (distance <= 0.001f) return takeoffPos; // 真上に居る。その場で跳ぶ

    // 手前で止めるぶんだけ引く。近すぎれば 0 ＝その場で跳んで叩く
    const float reach = (std::max)(0.0f, distance - bossAction.attackStandOff);
    return takeoffPos + toTarget * (reach / distance);
}



// 点がステージの円の外なら、中心へ向かって円の縁まで引き戻す
Vector3 BossActionSystem::ClampInsideArena(const Vector3 &point, const BattleContext &battle) const
{
    const Vector3 center(battle.arenaCenter);
    const Vector3 fromCenter = FlatToward(center, point);

    const float limit    = (std::max)(0.0f, battle.arenaRadius - kWallMargin);
    const float distance = fromCenter.Length();
    if (distance <= limit) return point;

    Vector3 clamped = center + fromCenter * (limit / distance);
    clamped.y       = point.y; // 高さは着地点の決め方に任せる
    return clamped;
}



// dir の向きへまっすぐ distance 進んだ地点を返す（後方ステップ・突進の下がる・突っ込む）
// 壁際でも向きは変えず、距離だけ縮めて壁の手前で止める
Vector3 BossActionSystem::StraightPointInArena(const Vector3 &from, const Vector3 &dir, float distance,
                                               const BattleContext &battle) const
{
    // 中心から見た今の位置（水平）
    const Vector3 offset = FlatToward(Vector3(battle.arenaCenter), from);
    const float   limit  = (std::max)(0.0f, battle.arenaRadius - kWallMargin);

    // まっすぐ進んで内側の円の縁に着くまでの距離を、|offset + dir × reach| = limit から求める
    // 縁の外から外向きに進むときは 0（その場で止まる）
    const float along = offset.Dot(dir);
    const float disc  = along * along - (offset.LengthSquared() - limit * limit);
    float       reach = 0.0f;
    if (disc > 0.0f) reach = (std::max)(0.0f, -along + std::sqrt(disc));

    return from + dir * (std::min)((std::max)(0.0f, distance), reach);
}



// BehaviorTreeSystem(L02) の後、JoltCharacterUpdateSystem(L03) の前。
REGISTER_LOGIC_SYSTEM(BossActionSystem, Priority::LogicStage::L02_PostUpdate);
