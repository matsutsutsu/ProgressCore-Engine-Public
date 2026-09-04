// ボスアクションシステムの実装（最小構成：待機・移動のみ）。
#include "BossActionSystem.h"

#include "Game/Logics/AI/Battle/BattleContext.h"
#include "Game/Logics/AI/BehaviorTree/Data/ActionRegistry.h"
#include "Game/Logics/AI/BehaviorTree/Data/BehaviorTreeComponents.h" // debugPaused
#include "Engine/Core/App/GameFlowState.h"                          // cinematic
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
    // 角度を -π～π の範囲にラップする
    float WrapAngle(float angle)
    {
        while (angle > DirectX::XM_PI)
            angle -= DirectX::XM_2PI;
        while (angle < -DirectX::XM_PI)
            angle += DirectX::XM_2PI;
        return angle;
    }
    
    // 目標方向を向く。lookDir はワールド座標系の方向ベクトル。
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
    
    // Jolt の CharacterVirtual に TransformComponent の回転を反映する
    void SyncCharacterRotation(const TransformComponent &trans, JPH::CharacterVirtual *character)
    {
        if (!character) return;
    
        character->SetRotation(JPH::Quat(trans.rotation.x, trans.rotation.y, trans.rotation.z, trans.rotation.w));
    }
} 



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

    Vector3 playerPos(battle.playerPos);

    // 演出中は全個体まとめて止める。World リソースなので個体ループの外で1回だけ読む。
    const bool cinematic = m_world->HasResource<GameFlowState>()
                        && m_world->GetResource<GameFlowState>().cinematic;

    ForEachWithID([&](EntityID entity,
                      CharacterMovementComponent &movement,
                      BossActionComponent &bossDef,
                      TransformComponent &trans,
                      const BossCommandComponent &cmd,
                      const JoltCharacterConfigComponent &config,
                      const JoltCharacterHandleComponent &joltHandle,
                      const TimeStateComponent &time) {
        if (!joltHandle.character) return;

        float dt = time.localDt;

        // 脳が止まっているか。
        //   演出中(cinematic)と、エディタでの手動停止(debugPaused)の両方を見る。
        //   BT は凍っても currentActionId を下ろさないので、ここで見ないと
        //   止めた瞬間の技をそのまま実行し続ける。
        const auto *bt     = m_world->GetComponent<BehaviorTreeComponent>(entity);
        const bool  frozen = cinematic || (bt && bt->debugPaused);

        // AIの意図(currentActionId) → ボスの物理ステートへ翻訳。
        //   凍っている間は意図を読まず、待機へ倒す。ProcessIdle が
        //   desiredVelocity を 0 にするので、その場に立ち止まる。
        //   解除後は BT(L02_Update) がこの層(L02_PostUpdate)より先に走って
        //   currentActionId を書き直すので、古い意図が復活することはない。
        const BossActionState next = frozen ? BossActionState::None
                                            : TranslateAction(cmd.currentActionId);

        // 入場処理：状態が変わったフレームだけ走る。
        //   突進・跳躍は「どこへ向かうか」をこの1回で決め、以後は更新しない。
        if (next != bossDef.currentState) OnEnterState(next, bossDef, trans, playerPos);

        bossDef.currentState = next;
        bossDef.actionTimer += dt;

        switch (bossDef.currentState)
        {
        case BossActionState::Move:
            ProcessMove(bossDef, trans, movement, config, joltHandle, playerPos, dt);
            break;
        case BossActionState::Melee:
            // 足を止めて斬る（アニメ本体は BT の SetTrigger で再生済み）
            ProcessMeleeHold(bossDef, trans, movement, joltHandle, playerPos, dt);
            break;
        case BossActionState::DashSlash:
            ProcessDashSlash(bossDef, trans, movement, joltHandle, playerPos, dt);
            break;
        case BossActionState::JumpAttack:
            ProcessJumpAttack(bossDef, trans, movement, joltHandle, playerPos, dt);
            break;
        case BossActionState::BackStep:
            ProcessBackStep(bossDef, trans, movement, joltHandle, playerPos, dt);
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

    // 上書きは書かない＝config.walkSpeed 基準へ戻る（要求は読み手が毎フレーム消費する）。
    movement.desiredVelocity = Vector3::Zero;
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
        movement.desiredVelocity    = Vector3::Zero;
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

void BossActionSystem::ProcessMeleeHold(BossActionComponent &bossDef, TransformComponent &trans,
    CharacterMovementComponent &movement, const JoltCharacterHandleComponent &joltHandle,
    const Vector3 &playerPos, float dt)
{
    // 近接攻撃中は足を止めて踏ん張る（薙ぎ払い）。
    //   アニメ本体は BT の SetTrigger で再生済み。当たり判定はアニメ内 HitBox イベントが握る。
    //   将来 踏み込み/突進 を足すときは、ここで cmd.currentActionId により前進を分岐する。
    bossDef.currentMoveSpeed = 0.0f;
    movement.desiredVelocity    = Vector3::Zero;

    // 斬る相手の方を向くのは「振りかぶり」の間だけ。
    //
    //   ここで回り続けると、振り切っている最中も硬直中もプレイヤーを追うので、
    //   横へ回り込んでも刃が付いてきて当たる。技を出したら向きは決め打ちにして、
    //   避けた側に「後隙を狩る」余地を残す。
    //   どこまで追うかは meleeTurnTime（既定 0.35 秒）で決める。
    if (bossDef.actionTimer < bossDef.meleeTurnTime)
    {
        Vector3 dirToPlayer = playerPos - trans.position;
        dirToPlayer.y       = 0.0f;
        TurnToward(trans, dirToPlayer, bossDef.turnSpeed, dt);
        SyncCharacterRotation(trans, joltHandle.character.GetPtr());
    }
}

BossActionState BossActionSystem::TranslateAction(ActionID actionId) const
{
    if (actionId == ::CCL::AI::A_Move)           return BossActionState::Move;
    if (actionId == ::CCL::AI::A_BossSlash)      return BossActionState::Melee;
    if (actionId == ::CCL::AI::A_BossDashSlash)  return BossActionState::DashSlash;
    if (actionId == ::CCL::AI::A_BossJumpAttack) return BossActionState::JumpAttack;
    if (actionId == ::CCL::AI::A_BossBackStep)   return BossActionState::BackStep;
    return BossActionState::None;
}


void BossActionSystem::OnEnterState(BossActionState next, BossActionComponent &bossDef,
                                    const TransformComponent &trans,
                                    const Vector3 &playerPos) const
{
    bossDef.actionTimer = 0.0f;

    // 進行方向はここで1回だけ決める。以降どれだけプレイヤーが動いても追わない。
    Vector3     toPlayer = playerPos - Vector3(trans.position);
    toPlayer.y           = 0.0f;
    const float distance = toPlayer.Length();
    if (distance > 0.001f)
    {
        toPlayer /= distance;
        bossDef.lockedDirection = toPlayer;
    }

    // 後方ステップだけは進行方向が逆。lockedDirection は常に「進む向き」なので、
    // 向き直す処理側（ProcessBackStep）が符号を反転して体の正面を決める。
    if (next == BossActionState::BackStep)
        bossDef.lockedDirection = -bossDef.lockedDirection;

    if (next == BossActionState::JumpAttack)
    {
        // 着地点も踏み切り前に確定させる。プレイヤーの真上へ吸い付くと回避不能になるため、
        // 「ここへ跳ぶ」を先に決めて、退く時間を与える。
        bossDef.jumpTakeoffPos = Vector3(trans.position);
        const float reach      = (std::min)(distance, bossDef.jumpMaxRange);
        bossDef.jumpLandingPos = bossDef.jumpTakeoffPos + bossDef.lockedDirection * reach;
    }
}


void BossActionSystem::ProcessDashSlash(BossActionComponent &bossDef, TransformComponent &trans,
    CharacterMovementComponent &movement, const JoltCharacterHandleComponent &joltHandle,
    const Vector3 &playerPos, float dt)
{
    const float t = bossDef.actionTimer;

    // --- 溜め：足を止めて正対する。踏み込む方向はこの間だけ更新し続ける ---
    if (t < bossDef.dashWindupTime)
    {
        bossDef.currentMoveSpeed = 0.0f;
        movement.desiredVelocity = Vector3::Zero;

        Vector3 dirToPlayer = playerPos - trans.position;
        dirToPlayer.y       = 0.0f;
        TurnToward(trans, dirToPlayer, bossDef.turnSpeed, dt);
        SyncCharacterRotation(trans, joltHandle.character.GetPtr());

        if (dirToPlayer.LengthSquared() > 0.001f)
        {
            dirToPlayer.Normalize();
            bossDef.lockedDirection = dirToPlayer; // 溜め終了時の向きが最終的な突進方向になる
        }
        return;
    }

    // --- 踏み込み：方向は固定。旋回しないので、横へ抜ければ避けられる ---
    if (t < bossDef.dashWindupTime + bossDef.dashDriveTime)
    {
        bossDef.currentMoveSpeed = bossDef.chargeSpeed;
        bossDef.moveDirection    = bossDef.lockedDirection;
        movement.desiredVelocity = bossDef.lockedDirection;

        // config.walkSpeed で頭打ちにしない。突進は「通常移動より速い」ことが技の中身なので、
        // 歩行上限で丸めると踏み込みが消える。
        movement.overrideSpeed = bossDef.chargeSpeed;
        return;
    }

    // --- 硬直：止まる。BT が param 秒だけこの技に留まるので、後隙はそこで決まる ---
    bossDef.currentMoveSpeed = 0.0f;
    movement.desiredVelocity = Vector3::Zero;
}


void BossActionSystem::ProcessJumpAttack(BossActionComponent &bossDef, TransformComponent &trans,
    CharacterMovementComponent &movement, const JoltCharacterHandleComponent &joltHandle,
    const Vector3 &playerPos, float dt)
{
    const float t = bossDef.actionTimer;

    // --- 溜め：踏み切るまで足を止めて正対する ---
    if (t < bossDef.jumpWindupTime)
    {
        bossDef.currentMoveSpeed = 0.0f;
        movement.desiredVelocity = Vector3::Zero;

        Vector3 dirToPlayer = playerPos - trans.position;
        dirToPlayer.y       = 0.0f;
        TurnToward(trans, dirToPlayer, bossDef.turnSpeed, dt);
        SyncCharacterRotation(trans, joltHandle.character.GetPtr());
        return;
    }

    const float flightTime    = (std::max)(0.05f, bossDef.jumpFlightTime);
    const float elapsedFlight = t - bossDef.jumpWindupTime;

    // --- 滞空：着地時刻から逆算した放物線を速度として与える ---
    if (elapsedFlight <= flightTime)
    {
        Vector3 horizontal = bossDef.jumpLandingPos - bossDef.jumpTakeoffPos;
        horizontal.y       = 0.0f;

        // 水平は等速。垂直は v(u) = 4h/T * (1 - 2u) で、u=0.5 が頂点、u=1 で高さが戻る。
        //   （速度を積分すると踏み切り高さへちょうど戻る形。着地フレームがずれない）
        const float u  = elapsedFlight / flightTime;
        Vector3     v  = horizontal / flightTime;
        v.y            = (4.0f * bossDef.jumpHeight / flightTime) * (1.0f - 2.0f * u);

        movement.use3DVelocity = true;  // 重力・水平計算を止めて、この速度をそのまま使う
        movement.velocity3D    = v;

        bossDef.currentMoveSpeed = horizontal.Length() / flightTime;
        bossDef.moveDirection    = bossDef.lockedDirection;
        return;
    }

    // --- 着地硬直：use3DVelocity を書かない＝重力が戻り、地面へ落ち着く ---
    bossDef.currentMoveSpeed = 0.0f;
    movement.desiredVelocity = Vector3::Zero;
}


void BossActionSystem::ProcessBackStep(BossActionComponent &bossDef, TransformComponent &trans,
    CharacterMovementComponent &movement, const JoltCharacterHandleComponent &joltHandle,
    const Vector3 &playerPos, float dt)
{
    const float t = bossDef.actionTimer;

    // プレイヤーは向き続ける。退く間も正対していないと、間合いを切って仕切り直す技ではなく
    // 「逃げ出した」に見える。旋回は退く向きの反対＝ lockedDirection の逆。
    TurnToward(trans, -bossDef.lockedDirection, bossDef.turnSpeed, dt);
    SyncCharacterRotation(trans, joltHandle.character.GetPtr());

    // --- 硬直：退き切ったあと動けない時間 ---
    //   長さは技の全体時間(BTノードの param)から backStepTime を引いたぶん。
    if (t >= bossDef.backStepTime)
    {
        bossDef.currentMoveSpeed = 0.0f;
        movement.desiredVelocity = Vector3::Zero;
        return;
    }

    // --- 後退：踏み切った速度から直線的に 0 まで落とす ---
    //   突進と違って溜めを持たない。被弾への反応なので、予備動作を入れると
    //   「殴られてから0.3秒棒立ちして退く」になり、避けている感じが出ない。
    const float u     = t / (std::max)(0.01f, bossDef.backStepTime);
    const float speed = bossDef.backStepSpeed * (1.0f - u);

    bossDef.currentMoveSpeed = speed;
    bossDef.moveDirection    = bossDef.lockedDirection;
    movement.desiredVelocity = bossDef.lockedDirection;

    // 突進と同じ理由で config.walkSpeed の頭打ちを掛けない。歩行上限で丸めると
    // 「素早く退く」という技の中身が消えて、ただの後ずさりになる。
    movement.overrideSpeed = speed;
}


// BehaviorTreeSystem(L02) の後、JoltCharacterUpdateSystem(L03) の前
REGISTER_LOGIC_SYSTEM(BossActionSystem, Priority::LogicStage::L02_PostUpdate);
