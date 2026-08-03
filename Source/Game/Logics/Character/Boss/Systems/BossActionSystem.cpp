/**
 * @file BossActionSystem.cpp
 * @brief ボスアクションシステムの実装（最小構成：待機・移動のみ）。
 */
#include "BossActionSystem.h"

#include "Game/Logics/AI/Battle/BattleContext.h"
#include "Game/Logics/AI/BehaviorTree/Data/ActionRegistry.h"

#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

#include <Jolt/Physics/Character/CharacterVirtual.h>

#include <cmath>
#include <algorithm>

#ifdef ENABLE_EDITOR
#include <imgui.h>
#endif

using namespace CCL::ECS;
using namespace DirectX::SimpleMath;

namespace
{
float WrapAngle(float angle)
{
    while (angle > DirectX::XM_PI)
        angle -= DirectX::XM_2PI;
    while (angle < -DirectX::XM_PI)
        angle += DirectX::XM_2PI;
    return angle;
}

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

void SyncCharacterRotation(const TransformComponent &trans, JPH::CharacterVirtual *character)
{
    if (!character) return;

    character->SetRotation(JPH::Quat(trans.rotation.x, trans.rotation.y, trans.rotation.z, trans.rotation.w));
}
} // namespace

void BossActionSystem::Update(float rawDt)
{
    if (!m_world->HasResource<BattleContext>()) return;
    const auto &battle = m_world->GetResource<BattleContext>();
    if (!battle.HasPlayer()) return;

    Vector3 playerPos(battle.playerPos);

    ForEach([&](CharacterMovementInputComponent &input,
                BossActionComponent &bossDef,
                TransformComponent &trans,
                const BossCommandComponent &cmd,
                const JoltCharacterConfigComponent &config,
                const JoltCharacterHandleComponent &joltHandle,
                const TimeStateComponent &time) {
        if (!joltHandle.character)
            return;

        float dt = time.localDt;

        // AIの意図(currentActionId) → ボスの物理ステートへ翻訳
        if (cmd.currentActionId == ::CCL::AI::A_Move)
            bossDef.currentState = BossActionState::Move;
        else if (::IsBossMeleeAction(cmd.currentActionId))
            bossDef.currentState = BossActionState::Melee;
        else
            bossDef.currentState = BossActionState::None;

        switch (bossDef.currentState)
        {
        case BossActionState::Move:
            ProcessMove(bossDef, trans, input, config, joltHandle, playerPos, dt);
            break;
        case BossActionState::Melee:
            // 近接攻撃中：足を止めて斬る（アニメ本体は BT の SetTrigger で再生済み）
            ProcessMeleeHold(bossDef, trans, input, joltHandle, playerPos, dt);
            break;
        default:
            ProcessIdle(bossDef, trans, input, joltHandle, playerPos, dt);
            break;
        }
    });
}


#ifdef ENABLE_EDITOR
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
#else
void BossActionSystem::OnGui() {}
#endif

void BossActionSystem::ProcessIdle(BossActionComponent &bossDef, TransformComponent &trans,
    CharacterMovementInputComponent &input, const JoltCharacterHandleComponent &joltHandle,
    const Vector3 &playerPos, float dt)
{
    bossDef.currentMoveSpeed = (std::max)(0.0f,
        bossDef.currentMoveSpeed - bossDef.deceleration * dt);

    input.desiredVelocity = Vector3::Zero;
    input.overrideSpeed   = -1.0f;
    
    Vector3 dirToPlayer = playerPos - trans.position;
    dirToPlayer.y       = 0.0f;
    TurnToward(trans, dirToPlayer, bossDef.idleTurnSpeed, dt);
    SyncCharacterRotation(trans, joltHandle.character.GetPtr());
}

void BossActionSystem::ProcessMove(BossActionComponent &bossDef, TransformComponent &trans,
    CharacterMovementInputComponent &input, const JoltCharacterConfigComponent &config,
    const JoltCharacterHandleComponent &joltHandle,
    const Vector3 &playerPos, float dt)
{
    Vector3 dirToPlayer = playerPos - trans.position;
    dirToPlayer.y       = 0.0f;

    if (dirToPlayer.LengthSquared() <= 0.001f)
    {
        bossDef.currentMoveSpeed = 0.0f;
        input.desiredVelocity    = Vector3::Zero;
        input.overrideSpeed      = -1.0f;
        return;
    }

    dirToPlayer.Normalize();
    bossDef.moveDirection = dirToPlayer;

    bossDef.currentMoveSpeed = (std::min)(bossDef.walkSpeed,
        bossDef.currentMoveSpeed + bossDef.acceleration * dt);

    input.desiredVelocity = dirToPlayer;
    input.overrideSpeed   = (std::min)(bossDef.currentMoveSpeed, config.walkSpeed);

    TurnToward(trans, dirToPlayer, bossDef.turnSpeed, dt);
    SyncCharacterRotation(trans, joltHandle.character.GetPtr());
}

void BossActionSystem::ProcessMeleeHold(BossActionComponent &bossDef, TransformComponent &trans,
    CharacterMovementInputComponent &input, const JoltCharacterHandleComponent &joltHandle,
    const Vector3 &playerPos, float dt)
{
    // 近接攻撃中は足を止めて踏ん張る（薙ぎ払い）。
    //   アニメ本体は BT の SetTrigger で再生済み。当たり判定はアニメ内 HitBox イベントが握る。
    //   ★将来 踏み込み/突進 を足すときは、ここで cmd.currentActionId により前進を分岐する。
    bossDef.currentMoveSpeed = 0.0f;
    input.desiredVelocity    = Vector3::Zero;
    input.overrideSpeed      = -1.0f;

    // 斬る相手（プレイヤー）の方を向く（移動中と同じ速さでしっかり正対）
    Vector3 dirToPlayer = playerPos - trans.position;
    dirToPlayer.y       = 0.0f;
    TurnToward(trans, dirToPlayer, bossDef.turnSpeed, dt);
    SyncCharacterRotation(trans, joltHandle.character.GetPtr());
}

// BehaviorTreeSystem(L02) の後、JoltCharacterUpdateSystem(L03) の前
REGISTER_LOGIC_SYSTEM(BossActionSystem, Priority::LogicStage::L02_PostUpdate);
