// ボスアクションシステムの実装。
#include "BossActionSystem.h"

#include "Game/Logics/AI/Battle/BattleContext.h"
#include "Game/Logics/AI/BehaviorTree/Data/ActionRegistry.h"
#include "Game/Logics/AI/BehaviorTree/Data/BehaviorTreeComponents.h" // debugPaused
#include "Engine/Core/App/GameFlowState.h"                           // cinematic
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
    /// 角度 [rad] を -π〜π に収める。yaw の差を「近い回り方」で取るために使う。
    float WrapAngle(float angle)
    {
        while (angle > DirectX::XM_PI)
            angle -= DirectX::XM_2PI;
        while (angle < -DirectX::XM_PI)
            angle += DirectX::XM_2PI;
        return angle;
    }

    /// lookDir（ワールド・水平）の方へ turnSpeed の速さで少しずつ向き直る。
    /// 1フレームで回り切らないので、呼び続けると滑らかに正対する。
    void TurnToward(TransformComponent &trans, const Vector3 &lookDir, float turnSpeed, float dt)
    {
        if (lookDir.LengthSquared() <= 0.001f) return;

        float targetYaw = atan2f(lookDir.x, lookDir.z);

        Vector3 currentForward = Vector3::Transform(Vector3::UnitZ, trans.rotation);
        float   currentYaw     = atan2f(currentForward.x, currentForward.z);

        float diff   = WrapAngle(targetYaw - currentYaw);
        float newYaw = currentYaw + diff * (std::min)(turnSpeed * dt, 1.0f);

        trans.rotation = Quaternion::CreateFromAxisAngle(Vector3::UnitY, newYaw);
        trans.isDirty  = true;
    }

    /// TransformComponent の回転を Jolt のキャラクターへ写す。
    /// TurnToward の後に呼ばないと、物理側の向きが古いまま残る。
    void SyncCharacterRotation(const TransformComponent &trans, JPH::CharacterVirtual *character)
    {
        if (!character) return;

        character->SetRotation(JPH::Quat(trans.rotation.x, trans.rotation.y, trans.rotation.z, trans.rotation.w));
    }
} // namespace

std::vector<CCL::ECS::TypeID> BossActionSystem::GetReadTypes() const
{
    auto types = Base::GetReadTypes();
    types.push_back(CCL::ECS::TypeInfo<BehaviorTreeComponent>::ID());
    return types;
}

void BossActionSystem::Update(float rawDt)
{
    if (!m_world->HasResource<BattleContext>()) return;
    const auto &battle = m_world->GetResource<BattleContext>();
    if (!battle.HasPlayer()) return;

    const Vector3 playerPos(battle.playerPos); // プレイヤーの足元（ワールド）

    // 演出中は全個体まとめて止める。World リソースなので個体ループの外で1回だけ読む。
    const bool cinematic = m_world->HasResource<GameFlowState>() && m_world->GetResource<GameFlowState>().cinematic;

    ForEachWithID([&](EntityID                            entity,
                      CharacterMovementComponent         &movement,
                      BossActionComponent                &bossDef,
                      TransformComponent                 &trans,
                      const BossCommandComponent         &cmd,
                      const JoltCharacterConfigComponent &config,
                      const JoltCharacterHandleComponent &joltHandle,
                      const TimeStateComponent           &time) {
        if (!joltHandle.character) return;

        const float dt = time.localDt; // この個体の時計の dt（ヒットストップ・スローを含む）

        // BT は凍っても currentActionId を下ろさないので、ここで見ないと止めた瞬間の技を
        // そのまま実行し続ける。凍っている間は意図を読まず待機へ倒す。
        const auto *bt     = m_world->GetComponent<BehaviorTreeComponent>(entity);
        const bool  frozen = cinematic || (bt && bt->debugPaused);

        // このフレームにやるべき動き。
        const BossActionState next = frozen ? BossActionState::None
                                            : TranslateAction(cmd.currentActionId);

        // 動く技は「どこへ向かうか」をこの1回で決め、以後は更新しない。
        if (next != bossDef.currentState) OnEnterState(next, cmd.currentActionId, bossDef, trans, playerPos);

        bossDef.currentState = next;
        bossDef.actionTimer += dt;

        switch (bossDef.currentState)
        {
        case BossActionState::Move:
            ProcessMove(bossDef, trans, movement, config, joltHandle, playerPos, dt);
            break;
        case BossActionState::BackStep:
            ProcessBackStep(bossDef, trans, movement, joltHandle, playerPos, dt);
            break;
        case BossActionState::Leap:
            ProcessLeap(bossDef, trans, movement, joltHandle, playerPos, battle, dt);
            break;
        default:
            ProcessIdle(bossDef, trans, movement, joltHandle, playerPos, dt);
            break;
        }
    });
}

void BossActionSystem::OnGui()
{
    if (!ImGui::CollapsingHeader("Boss Action (Position Debugger)"))
        return;

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

void BossActionSystem::ProcessIdle(BossActionComponent &bossDef, TransformComponent &trans,
                                   CharacterMovementComponent &movement, const JoltCharacterHandleComponent &joltHandle,
                                   const Vector3 &playerPos, float dt)
{
    bossDef.currentMoveSpeed = (std::max)(0.0f,
                                          bossDef.currentMoveSpeed - bossDef.deceleration * dt);

    // 上書きは書かない＝config.walkSpeed 基準へ戻る。
    movement.desiredVelocity = Vector3::Zero;

    // 足は止めるが目は離さない。旋回を移動中より遅くする（idleTurnSpeed < turnSpeed）のが
    // 要点で、即座に正対すると背後を取る価値が消え、回らないと置物に見える。
    Vector3 dirToPlayer = playerPos - trans.position;
    dirToPlayer.y       = 0.0f;
    TurnToward(trans, dirToPlayer, bossDef.idleTurnSpeed, dt);
    SyncCharacterRotation(trans, joltHandle.character.GetPtr());
}

void BossActionSystem::ProcessMove(BossActionComponent &bossDef, TransformComponent &trans,
                                   CharacterMovementComponent &movement, const JoltCharacterConfigComponent &config,
                                   const JoltCharacterHandleComponent &joltHandle,
                                   const Vector3 &playerPos, float dt)
{
    Vector3 dirToPlayer = playerPos - trans.position;
    dirToPlayer.y       = 0.0f;

    if (dirToPlayer.LengthSquared() <= 0.001f)
    {
        bossDef.currentMoveSpeed = 0.0f;
        movement.desiredVelocity = Vector3::Zero;
        return;
    }

    dirToPlayer.Normalize();

    bossDef.currentMoveSpeed = (std::min)(bossDef.walkSpeed,
                                          bossDef.currentMoveSpeed + bossDef.acceleration * dt);

    movement.desiredVelocity = dirToPlayer;
    movement.overrideSpeed   = (std::min)(bossDef.currentMoveSpeed, config.walkSpeed);

    TurnToward(trans, dirToPlayer, bossDef.turnSpeed, dt);
    SyncCharacterRotation(trans, joltHandle.character.GetPtr());
}

BossActionState BossActionSystem::TranslateAction(ActionID actionId) const
{
    if (actionId == ::CCL::AI::A_Move) return BossActionState::Move;
    if (actionId == ::CCL::AI::A_BossBackStep) return BossActionState::BackStep;
    if (actionId == ::CCL::AI::A_BossLeapEscape) return BossActionState::Leap;
    if (actionId == ::CCL::AI::A_BossLeapAttack) return BossActionState::Leap;
    return BossActionState::None;
}

void BossActionSystem::OnEnterState(BossActionState next, ActionID actionId, BossActionComponent &bossDef,
                                    const TransformComponent &trans,
                                    const Vector3            &playerPos) const
{
    bossDef.actionTimer = 0.0f;

    // 進行方向はここで1回だけ決める。以降プレイヤーが動いても追わない。
    Vector3 toPlayer     = playerPos - Vector3(trans.position);
    toPlayer.y           = 0.0f;
    const float distance = toPlayer.Length();
    if (distance > 0.001f)
    {
        toPlayer /= distance;
        bossDef.lockedDirection = toPlayer;
    }

    // 後方ステップだけは進行方向が逆（lockedDirection は常に「進む向き」）。
    if (next == BossActionState::BackStep)
        bossDef.lockedDirection = -bossDef.lockedDirection;

    // 跳躍は「どの技か」で着地点の決め方と調整値を選ぶ。着地点そのものはまだ決めない
    // （踏み切りの瞬間に BeginLeapFlight が決める）。
    if (next == BossActionState::Leap)
    {
        bossDef.leapPhase       = BossLeapPhase::Windup;
        bossDef.leapTouchedDown = false;

        if (actionId == ::CCL::AI::A_BossLeapEscape)
        {
            bossDef.leapLanding    = BossLeapLanding::AwayToCenter;
            bossDef.leapWindupTime = bossDef.escapeWindupTime;
            bossDef.leapFlightTime = bossDef.escapeFlightTime;
            bossDef.leapHeight     = bossDef.escapeHeight;
            bossDef.leapLandAttack = false;
        }
        else if (actionId == ::CCL::AI::A_BossLeapAttack)
        {
            bossDef.leapLanding    = BossLeapLanding::TowardTarget;
            bossDef.leapWindupTime = bossDef.attackWindupTime;
            bossDef.leapFlightTime = bossDef.attackFlightTime;
            bossDef.leapHeight     = bossDef.attackHeight;
            bossDef.leapLandAttack = true;
        }
    }
}

void BossActionSystem::ProcessBackStep(BossActionComponent &bossDef, TransformComponent &trans,
                                       CharacterMovementComponent &movement, const JoltCharacterHandleComponent &joltHandle,
                                       const Vector3 &playerPos, float dt)
{
    const float t = bossDef.actionTimer; // 後方ステップに入ってからの経過 [秒]

    // 退く間も正対し続ける。向いていないと、間合いを切って仕切り直す技ではなく
    // 「逃げ出した」に見える。
    TurnToward(trans, -bossDef.lockedDirection, bossDef.turnSpeed, dt);
    SyncCharacterRotation(trans, joltHandle.character.GetPtr());

    // 硬直。長さは技の全体時間(BTノードの param)から backStepTime を引いたぶん。
    if (t >= bossDef.backStepTime)
    {
        bossDef.currentMoveSpeed = 0.0f;
        movement.desiredVelocity = Vector3::Zero;
        return;
    }

    // 後退。弧を描いて「強く引かれる」形にする。地上を滑らせるとどんなに速くしても
    // クリップの足と合わず「後ろ歩き」に見えるが、空中なら接地感が問われない。
    // 溜めを持たないのは被弾への反応だから（予備動作を入れると避けている感じが出ない）。
    // 垂直の v(u) = 4h/T * (1 - 2u) は積分すると踏み切り高さへちょうど戻るので、
    // 着地の時刻が backStepTime からずれない。
    const float T = (std::max)(0.01f, bossDef.backStepTime); // 退く長さ [秒]
    const float u = t / T;                                   // 退きの進み具合 0〜1

    // 水平は直線減速のまま。退く距離 = backStepSpeed * backStepTime / 2 という
    // 読める式を残す（間合いの設計はこの数字でやる）。
    const float speed = bossDef.backStepSpeed * (1.0f - u);

    Vector3 v = bossDef.lockedDirection * speed;
    v.y       = (4.0f * bossDef.backStepHeight / T) * (1.0f - 2.0f * u);

    // 重力と水平計算を止めてこの速度をそのまま使う。歩行上限で丸めると
    // 「素早く退く」という技の中身が消えてただの後ずさりになる。
    movement.use3DVelocity = true;
    movement.velocity3D    = v;

    bossDef.currentMoveSpeed = speed;
    bossDef.moveDirection    = bossDef.lockedDirection;
}

void BossActionSystem::ProcessLeap(BossActionComponent &bossDef, TransformComponent &trans,
                                   CharacterMovementComponent &movement, const JoltCharacterHandleComponent &joltHandle,
                                   const Vector3 &playerPos, const BattleContext &battle, float dt)
{
    JPH::CharacterVirtual *ch = joltHandle.character.GetPtr();

    // 今の位置。Transform ではなく Jolt から取る（こちらが物理の正で、1フレーム遅れない）。
    const JPH::RVec3 jp = ch->GetPosition();
    const Vector3    now(static_cast<float>(jp.GetX()), static_cast<float>(jp.GetY()), static_cast<float>(jp.GetZ()));

    const float t = bossDef.actionTimer; // 跳躍に入ってからの経過 [秒]

    // 溜め：足を止めて相手を睨む。着地点はまだ決めない。
    if (t < bossDef.leapWindupTime)
    {
        bossDef.leapPhase        = BossLeapPhase::Windup;
        bossDef.currentMoveSpeed = 0.0f;
        movement.desiredVelocity = Vector3::Zero;

        Vector3 dirToPlayer = playerPos - trans.position;
        dirToPlayer.y       = 0.0f;
        TurnToward(trans, dirToPlayer, bossDef.turnSpeed, dt);
        SyncCharacterRotation(trans, ch);
        return;
    }

    if (bossDef.leapPhase == BossLeapPhase::Windup)
    {
        BeginLeapFlight(bossDef, now, playerPos, battle);
        bossDef.leapPhase = BossLeapPhase::Flight;
    }

    if (bossDef.leapPhase == BossLeapPhase::Flight)
    {
        const float flightTime = (std::max)(0.05f, bossDef.leapFlightTime);
        const float u          = (std::min)((t - bossDef.leapWindupTime) / flightTime, 1.0f); // 飛行の進み具合 0〜1

        // このフレームの積分が終わった時点で居るべき位置へ届く速度。Jolt 側と同じ dt で
        // 割らないと、スロー中やヒットストップ中に行き過ぎる（JoltCharacterUpdateSystem の
        // safeDt と同じ式）。
        const float stepDt = (std::max)(0.0001f, (std::min)(dt, 0.0333f)); // Jolt が積分に使う dt
        Vector3     v      = (LeapPositionAt(bossDef, u) - now) / stepDt;      // 今あるべき位置へ届く速度

        // 空中で相手や壁に引っかかったあと、遅れを1フレームで取り返そうとして
        // 瞬間移動に見えないようにする蓋。普段の飛行速度（60m/s 前後）より十分上。
        constexpr float kMaxLeapSpeed = 150.0f;
        if (v.LengthSquared() > kMaxLeapSpeed * kMaxLeapSpeed)
        {
            v.Normalize();
            v *= kMaxLeapSpeed;
        }

        movement.use3DVelocity = true; // 毎フレーム書く。書くのをやめたフレームに重力で落ちる
        movement.velocity3D    = v;

        // 跳んでいる向きへ体を向ける。相手を向いたまま飛ぶと、頭上を越える軌道で
        // 背中から着地する。
        TurnToward(trans, bossDef.lockedDirection, bossDef.turnSpeed, dt);
        SyncCharacterRotation(trans, ch);

        // アニメの Idle/Move を動かさない（空中の絵は IsLeaping 側の遷移が持つ）。
        bossDef.currentMoveSpeed = 0.0f;
        bossDef.moveDirection    = bossDef.lockedDirection;

        if (u >= 1.0f) bossDef.leapPhase = BossLeapPhase::Recovery;
        return;
    }

    // 着地硬直。着地した最初のフレームだけ速度を 0 で上書きする。飛行中の速度が
    // currentVelocity に残っており、放っておくと減速のぶんだけ地面を滑る。
    // 2フレーム目以降は書かない＝重力が戻り、段差があってもそのまま接地する。
    if (!bossDef.leapTouchedDown)
    {
        movement.use3DVelocity  = true;
        movement.velocity3D     = Vector3::Zero;
        bossDef.leapTouchedDown = true;
    }

    bossDef.currentMoveSpeed = 0.0f;
    movement.desiredVelocity = Vector3::Zero;

    // 着地攻撃の硬直は向きを固定する。着地の攻撃判定が出ている間に相手を追うと、
    // 横へ抜けても判定が付いてきて後隙を狩れない。
    if (bossDef.leapLandAttack) return;

    Vector3 dirToPlayer = playerPos - trans.position;
    dirToPlayer.y       = 0.0f;
    TurnToward(trans, dirToPlayer, bossDef.idleTurnSpeed, dt);
    SyncCharacterRotation(trans, ch);
}

void BossActionSystem::BeginLeapFlight(BossActionComponent &bossDef, const Vector3 &takeoffPos,
                                       const Vector3 &playerPos, const BattleContext &battle) const
{
    bossDef.leapTakeoffPos = takeoffPos;

    switch (bossDef.leapLanding)
    {
    case BossLeapLanding::AwayToCenter:
        bossDef.leapLandingPos = AwayToCenterLandingPoint(bossDef, takeoffPos, playerPos, battle);
        break;
    case BossLeapLanding::TowardTarget:
        bossDef.leapLandingPos = TowardTargetLandingPoint(bossDef, takeoffPos, playerPos);
        break;
    }

    // 相手が壁際に立っていると、その手前でも壁の外に出ることがある。空中で見えない壁に
    // 当たると位置の追従が壁を押し続け、着地の時刻と攻撃判定がずれる。
    bossDef.leapLandingPos = ClampInsideArena(bossDef.leapLandingPos, battle);

    Vector3 travel = bossDef.leapLandingPos - takeoffPos;
    travel.y       = 0.0f;
    if (travel.LengthSquared() > 0.0001f)
    {
        travel.Normalize();
        bossDef.lockedDirection = travel;
    }
}

Vector3 BossActionSystem::AwayToCenterLandingPoint(const BossActionComponent &bossDef, const Vector3 &takeoffPos,
                                                   const Vector3 &playerPos, const BattleContext &battle) const
{
    Vector3 center(battle.arenaCenter);
    center.y = takeoffPos.y; // 平らなステージ前提。起伏を付けるなら接地点を引く

    // 相手→中央の向きへ抜ける。中央のさらに向こうへ降りるので、相手から見ると
    // 頭上を越えて距離を取られる形になる。
    Vector3 away = center - playerPos;
    away.y       = 0.0f;

    // 相手がちょうど中央に居ると向きが決まらない。その場合は自分→中央で代用する。
    if (away.LengthSquared() <= 0.0001f)
    {
        away   = center - takeoffPos;
        away.y = 0.0f;
    }
    if (away.LengthSquared() <= 0.0001f) return center; // 全員が中央に重なっている
    away.Normalize();

    // 0.9 で頭打ち。1 に近いと反対側の端に降り、今度はそちらで追い込まれる。
    const float ratio = (std::max)(0.0f, (std::min)(bossDef.escapeLandingRatio, 0.9f));
    return center + away * (battle.arenaRadius * ratio);
}

Vector3 BossActionSystem::TowardTargetLandingPoint(const BossActionComponent &bossDef, const Vector3 &takeoffPos,
                                                   const Vector3 &playerPos) const
{
    Vector3 toTarget = playerPos - takeoffPos;
    toTarget.y       = 0.0f;

    const float distance = toTarget.Length();
    if (distance <= 0.001f) return takeoffPos; // 真上に居る。その場で跳ぶ

    // 手前で止めるぶんだけ引く。近すぎれば 0 ＝その場で跳んで叩く。
    const float reach = (std::max)(0.0f, distance - bossDef.attackStandOff);
    return takeoffPos + toTarget * (reach / distance);
}

Vector3 BossActionSystem::ClampInsideArena(const Vector3 &point, const BattleContext &battle) const
{
    // カプセル半径（1.4m）に余裕を足した分だけ壁から離す。
    constexpr float kWallMargin = 3.0f;

    const Vector3 center(battle.arenaCenter);
    Vector3       fromCenter = point - center;
    fromCenter.y             = 0.0f;

    const float limit    = (std::max)(0.0f, battle.arenaRadius - kWallMargin);
    const float distance = fromCenter.Length();
    if (distance <= limit) return point;

    Vector3 clamped = center + fromCenter * (limit / distance);
    clamped.y       = point.y; // 高さは着地点の決め方に任せる
    return clamped;
}

Vector3 BossActionSystem::LeapPositionAt(const BossActionComponent &bossDef, float u) const
{
    Vector3 p = Vector3::Lerp(bossDef.leapTakeoffPos, bossDef.leapLandingPos, u);

    // 4h*u(1-u) は u=0.5 で h、両端で 0。着地の高さは踏み切りと同じに戻る。
    p.y += 4.0f * bossDef.leapHeight * u * (1.0f - u);
    return p;
}

// BehaviorTreeSystem(L02) の後、JoltCharacterUpdateSystem(L03) の前。
REGISTER_LOGIC_SYSTEM(BossActionSystem, Priority::LogicStage::L02_PostUpdate);
