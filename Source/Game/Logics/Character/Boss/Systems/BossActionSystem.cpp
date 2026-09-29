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

    /// lookDir（ワールド・水平）の方へ turnSpeed の速さで少しずつ向き直り、Jolt へも写す。
    /// 1フレームで回り切らないので、呼び続けると滑らかに正対する。
    ///
    /// 描画と物理の向きを必ず一緒に動かすため1つにしてある。別々の関数だと、
    /// 片方だけ呼んだ場所で物理側の向きが古いまま取り残される。
    void FaceToward(TransformComponent &trans, const Vector3 &lookDir, float turnSpeed, float dt,
                    JPH::CharacterVirtual *character)
    {
        if (lookDir.LengthSquared() <= 0.001f) return;

        float targetYaw = atan2f(lookDir.x, lookDir.z);

        Vector3 currentForward = Vector3::Transform(Vector3::UnitZ, trans.rotation);
        float   currentYaw     = atan2f(currentForward.x, currentForward.z);

        float diff   = WrapAngle(targetYaw - currentYaw);
        float newYaw = currentYaw + diff * (std::min)(turnSpeed * dt, 1.0f);

        trans.rotation = Quaternion::CreateFromAxisAngle(Vector3::UnitY, newYaw);
        trans.isDirty  = true;

        if (character)
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
        if (next != bossDef.currentState)
            OnEnterState(next, cmd.currentActionId, cmd.currentActionDuration, bossDef, trans, playerPos);

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
        case BossActionState::Dash:
            ProcessDash(bossDef, trans, movement, joltHandle, playerPos, battle, dt);
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
    FaceToward(trans, dirToPlayer, bossDef.idleTurnSpeed, dt, joltHandle.character.GetPtr());
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

    FaceToward(trans, dirToPlayer, bossDef.turnSpeed, dt, joltHandle.character.GetPtr());
}

BossActionState BossActionSystem::TranslateAction(ActionID actionId) const
{
    if (actionId == A_Move) return BossActionState::Move;
    if (actionId == A_BossBackStep) return BossActionState::BackStep;
    if (actionId == A_BossLeapEscape) return BossActionState::Leap;
    if (actionId == A_BossLeapAttack) return BossActionState::Leap;
    if (actionId == A_BossAirHover) return BossActionState::Leap;
    if (actionId == A_BossDashAttack) return BossActionState::Dash;
    return BossActionState::None;
}

void BossActionSystem::OnEnterState(BossActionState next, ActionID actionId, float actionDuration,
                                    BossActionComponent &bossDef,
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
        bossDef.leapHoverTime   = 0.0f;  // 頂点で止まるのは滞空だけ
        bossDef.leapLandAttack  = false; // 着地に判定を出すのは跳躍攻撃だけ

        if (actionId == A_BossLeapEscape)
        {
            bossDef.leapDef     = bossDef.escapeLeap;
            bossDef.leapLanding = BossLeapLanding::AwayToCenter;
        }
        else if (actionId == A_BossLeapAttack)
        {
            bossDef.leapDef        = bossDef.attackLeap;
            bossDef.leapLanding    = BossLeapLanding::TowardTarget;
            bossDef.leapLandAttack = true;
        }
        else if (actionId == A_BossAirHover)
        {
            bossDef.leapDef       = bossDef.hoverLeap;
            bossDef.leapLanding   = BossLeapLanding::InPlace;
            // 頂点に留まる長さは、BT の全体時間から他の段を引いた残り。長さの数字を
            // BT ノードの1か所に置くため（プレハブ側にも持つと、片方だけ直してずれる）。
            const BossLeapDef &def = bossDef.hoverLeap;
            bossDef.leapHoverTime =
                (std::max)(0.0f, actionDuration - def.windup - def.flight - bossDef.hoverLandRecovery);
        }
    }

    // 突進は下がる向き（lockedDirection の逆）だけをここで決める。下がり始めの地点は
    // Jolt の位置で取りたいので最初のフレームに、狙いは予告の終わりに決める。
    if (next == BossActionState::Dash)
    {
        bossDef.dashPhase   = BossDashPhase::Retreat;
        bossDef.dashStarted = false;
    }
}

void BossActionSystem::ProcessBackStep(BossActionComponent &bossDef, TransformComponent &trans,
                                       CharacterMovementComponent &movement, const JoltCharacterHandleComponent &joltHandle,
                                       const Vector3 &playerPos, float dt)
{
    const float t = bossDef.actionTimer; // 後方ステップに入ってからの経過 [秒]

    // 退く間も正対し続ける。向いていないと、間合いを切って仕切り直す技ではなく
    // 「逃げ出した」に見える。
    FaceToward(trans, -bossDef.lockedDirection, bossDef.turnSpeed, dt, joltHandle.character.GetPtr());

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
    if (t < bossDef.leapDef.windup)
    {
        bossDef.leapPhase        = BossLeapPhase::Windup;
        bossDef.currentMoveSpeed = 0.0f;
        movement.desiredVelocity = Vector3::Zero;

        Vector3 dirToPlayer = playerPos - trans.position;
        dirToPlayer.y       = 0.0f;
        FaceToward(trans, dirToPlayer, bossDef.turnSpeed, dt, ch);
        return;
    }

    if (bossDef.leapPhase == BossLeapPhase::Windup)
    {
        BeginLeapFlight(bossDef, now, playerPos, battle);
        bossDef.leapPhase = BossLeapPhase::Flight;
    }

    if (bossDef.leapPhase != BossLeapPhase::Recovery)
    {
        const float flightTime = (std::max)(0.05f, bossDef.leapDef.flight);
        const float elapsed    = t - bossDef.leapDef.windup; // 踏み切ってからの経過 [秒]

        // 滞空は「曲線の進み具合を頂点で止める」だけで表す。上がり方も降り方も跳躍と
        // 同じ式のままなので、留まる長さを変えても踏み切りと着地の速さは変わらない。
        const float hold   = (std::max)(0.0f, bossDef.leapHoverTime); // 頂点に留まる長さ [秒]
        const float toApex = flightTime * 0.5f;                       // 頂点に着くまで [秒]

        float u        = 0.0f; // 飛行の進み具合 0〜1
        bool  hovering = false;
        if (hold <= 0.0f || elapsed < toApex)
            u = (std::min)(elapsed / flightTime, 1.0f); // 上昇（留まらない跳躍はそのまま着地まで）
        else if (elapsed < toApex + hold)
        {
            u        = 0.5f; // 頂点で足踏み。高さも水平位置もここで止まる
            hovering = true;
        }
        else
            u = (std::min)((elapsed - hold) / flightTime, 1.0f); // 下降（留まった分だけ時計を戻す）

        bossDef.leapPhase = hovering ? BossLeapPhase::Hover : BossLeapPhase::Flight;

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
        // 背中から着地する。滞空だけは進まないので、浮いたまま相手を向き続ける。
        Vector3 lookDir = bossDef.lockedDirection;
        if (bossDef.leapLanding == BossLeapLanding::InPlace)
        {
            lookDir   = playerPos - trans.position;
            lookDir.y = 0.0f;
        }
        FaceToward(trans, lookDir, bossDef.turnSpeed, dt, ch);

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
    FaceToward(trans, dirToPlayer, bossDef.idleTurnSpeed, dt, ch);
}

void BossActionSystem::ProcessDash(BossActionComponent &bossDef, TransformComponent &trans,
                                   CharacterMovementComponent &movement, const JoltCharacterHandleComponent &joltHandle,
                                   const Vector3 &playerPos, const BattleContext &battle, float dt)
{
    JPH::CharacterVirtual *ch = joltHandle.character.GetPtr();

    // 今の位置。跳躍と同じく Jolt から取る。
    const JPH::RVec3 jp = ch->GetPosition();
    const Vector3    now(static_cast<float>(jp.GetX()), static_cast<float>(jp.GetY()), static_cast<float>(jp.GetZ()));

    const float t = bossDef.actionTimer; // 突進に入ってからの経過 [秒]

    // 最初のフレーム：下がり始めの地点と、下がり切る地点を決める。
    //   lockedDirection は相手への向きなので、その逆へ下がる。
    if (!bossDef.dashStarted)
    {
        bossDef.dashStartPos = now;

        Vector3 aim = now - bossDef.lockedDirection * bossDef.dashRetreatDistance;
        aim.y       = now.y + bossDef.dashRetreatHeight;

        // 壁際で下がると壁の外に出る。高さは ClampInsideArena が残す。
        bossDef.dashAimPos  = ClampInsideArena(aim, battle);
        bossDef.dashStarted = true;
    }

    // 段は一方通行。時刻が閾値を越えたフレームに次へ進む（1フレームで複数進むこともある）。
    const float retreatEnd = (std::max)(0.05f, bossDef.dashRetreatTime);
    const float aimEnd     = retreatEnd + (std::max)(0.0f, bossDef.dashAimTime);

    if (bossDef.dashPhase == BossDashPhase::Retreat && t >= retreatEnd)
        bossDef.dashPhase = BossDashPhase::Aim;

    if (bossDef.dashPhase == BossDashPhase::Aim && t >= aimEnd)
    {
        BeginDashCharge(bossDef, playerPos, battle);
        bossDef.dashPhase = BossDashPhase::Charge;
    }

    // 突っ込む長さは BeginDashCharge が距離から決めるので、ここから先の時刻はその後で出す。
    const float chargeEnd = aimEnd + bossDef.dashChargeTime;
    const float travelEnd = chargeEnd + (std::max)(0.0f, bossDef.dashTravelTime);
    const float stopEnd   = travelEnd + (std::max)(0.0f, bossDef.dashStopTime);

    if (bossDef.dashPhase == BossDashPhase::Charge && t >= chargeEnd)
        bossDef.dashPhase = BossDashPhase::Travel;

    if (bossDef.dashPhase == BossDashPhase::Travel && t >= travelEnd)
        bossDef.dashPhase = BossDashPhase::Stop;

    if (bossDef.dashPhase == BossDashPhase::Stop && t >= stopEnd)
    {
        // 止まった最初のフレームだけ速度を 0 で上書きする（跳躍の着地と同じ理由で、
        // 走りの速度が残ると地面を滑る）。
        bossDef.dashPhase      = BossDashPhase::Recovery;
        movement.use3DVelocity = true;
        movement.velocity3D    = Vector3::Zero;
    }

    // 硬直。向きは突進の向きのまま固定する。相手を追って向き直ると、脇へ避けた相手へ
    // 振り向きながら止まり、避けたのに狙われ続けているように見える。
    if (bossDef.dashPhase == BossDashPhase::Recovery)
    {
        bossDef.currentMoveSpeed = 0.0f;
        movement.desiredVelocity = Vector3::Zero;
        return;
    }

    // 着地後の前進と減速。位置ではなく速度で進める。行き先の点が無い（方向と速さだけの）
    // 動きなので、壁に当たったら壁に沿って滑り、そのまま走り続ける。
    if (bossDef.dashPhase == BossDashPhase::Travel || bossDef.dashPhase == BossDashPhase::Stop)
    {
        float speed = bossDef.dashTravelSpeed;
        if (bossDef.dashPhase == BossDashPhase::Stop)
        {
            // 止まる段：走る速さから 0 へ直線で落とす。
            const float stopTime = (std::max)(0.001f, bossDef.dashStopTime);
            const float u        = (std::min)(1.0f, (t - travelEnd) / stopTime);
            speed *= (1.0f - u);
        }

        FaceToward(trans, bossDef.lockedDirection, bossDef.turnSpeed, dt, ch);

        movement.use3DVelocity = true; // 高さは突っ込んだ時点で地面に揃っているので y は 0
        movement.velocity3D    = bossDef.lockedDirection * speed;

        bossDef.currentMoveSpeed = 0.0f; // 走りの絵は段が運ぶ（Idle/Move は動かさない）
        bossDef.moveDirection    = bossDef.lockedDirection;
        return;
    }

    // このフレームに居るべき位置と、向く方向。
    Vector3 want;
    Vector3 lookDir;
    switch (bossDef.dashPhase)
    {
    case BossDashPhase::Retreat: {
        // 速く下がり始めて、狙う位置で止まる（1-(1-u)^2 は u=1 で傾き 0）。
        const float u     = t / retreatEnd;
        const float eased = 1.0f - (1.0f - u) * (1.0f - u);
        want              = Vector3::Lerp(bossDef.dashStartPos, bossDef.dashAimPos, eased);
        lookDir           = playerPos - now; // 下がりながらも相手を向き続ける
        break;
    }
    case BossDashPhase::Aim:
        want    = bossDef.dashAimPos;
        lookDir = playerPos - now; // 予告。向きが突進の方向になるので、プレイヤーはここを読む
        break;

    default: { // Charge：空中から地面の終点へ等速でまっすぐ突っ込む
        const float s = (std::min)(1.0f, (t - aimEnd) / (std::max)(0.001f, bossDef.dashChargeTime));
        want          = Vector3::Lerp(bossDef.dashAimPos, bossDef.dashEndPos, s);
        lookDir       = bossDef.lockedDirection;
        break;
    }
    }
    lookDir.y = 0.0f;
    FaceToward(trans, lookDir, bossDef.turnSpeed, dt, ch);

    // 跳躍と同じく、今あるべき位置へ届く速度を与える（Jolt と同じ dt で割る）。
    const float stepDt = (std::max)(0.0001f, (std::min)(dt, 0.0333f));
    Vector3     v      = (want - now) / stepDt;

    // 引っかかった遅れを1フレームで取り返して瞬間移動に見えないようにする蓋。
    // 突進の速さを上げたときに蓋で頭打ちにならないよう、突進の倍は許す。
    const float maxSpeed = (std::max)(150.0f, bossDef.dashSpeed * 2.0f);
    if (v.LengthSquared() > maxSpeed * maxSpeed)
    {
        v.Normalize();
        v *= maxSpeed;
    }

    movement.use3DVelocity = true; // 毎フレーム書く。書くのをやめたフレームに重力で落ちる
    movement.velocity3D    = v;

    // アニメの Idle/Move を動かさない（突進の絵はトリガーと段が運ぶ）。
    bossDef.currentMoveSpeed = 0.0f;
    bossDef.moveDirection    = bossDef.lockedDirection;
}

void BossActionSystem::BeginDashCharge(BossActionComponent &bossDef, const Vector3 &playerPos,
                                       const BattleContext &battle) const
{
    // 狙うのは相手への「方向」だけ。突っ込む距離は相手の位置に関係なく dashDistance で決まり、
    // その後も同じ方向へ走り続ける（Travel）。
    //   相手の位置で止めないのは、この技が近距離の攻撃と間合いを離す移動を兼ねるから。
    //   当たっても外れても、走り抜けた先で相手との距離が開く。
    Vector3 toTarget = playerPos - bossDef.dashAimPos;
    toTarget.y       = 0.0f;

    // 真下に相手が居ると向きが決まらない。そのときは下がる前の向き（相手への向き）で突っ込む。
    const float toTargetLen = toTarget.Length();
    Vector3     dir         = (toTargetLen > 0.001f) ? toTarget / toTargetLen : bossDef.lockedDirection;

    // 高さは下がり始めの地点＝地面に揃える（平らなステージ前提）。空中から地面へ下りながら進む。
    Vector3 end = bossDef.dashAimPos + dir * bossDef.dashDistance;
    end.y       = bossDef.dashStartPos.y;

    bossDef.dashEndPos      = ClampInsideArena(end, battle);
    bossDef.lockedDirection = dir;

    // 時間は実際に走る距離（高さを含む）から出す。速さを一定に保つため。
    const float length     = Vector3::Distance(bossDef.dashAimPos, bossDef.dashEndPos);
    bossDef.dashChargeTime = length / (std::max)(1.0f, bossDef.dashSpeed);
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
    case BossLeapLanding::InPlace:
        bossDef.leapLandingPos = takeoffPos; // 上がって同じ所へ降りる（滞空）
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
    p.y += 4.0f * bossDef.leapDef.height * u * (1.0f - u);
    return p;
}

// BehaviorTreeSystem(L02) の後、JoltCharacterUpdateSystem(L03) の前。
REGISTER_LOGIC_SYSTEM(BossActionSystem, Priority::LogicStage::L02_PostUpdate);
