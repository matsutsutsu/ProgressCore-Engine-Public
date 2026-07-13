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

#include <cmath>
#include <algorithm>

#ifdef ENABLE_EDITOR
#include <imgui.h>
#endif

using namespace CCL::ECS;
using namespace DirectX::SimpleMath;

namespace
{
// 角度差を -180度〜180度に丸める（最短回転のため）
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
} // namespace

void BossActionSystem::Update(float rawDt)
{
    if (!_world->HasResource<BattleContext>()) return;
    const auto &battle = _world->GetResource<BattleContext>();
    if (!battle.HasPlayer()) return;

    Vector3 playerPos(battle.playerPos);

    ForEach([&](TransformComponent &trans, BossActionComponent &bossDef,
                const BossCommandComponent &cmd, const TimeStateComponent &time) {
        float dt = time.localDt;

        // --- コマンド受付：意図に応じてステートを決める ---
        bossDef.currentState = (cmd.currentActionId == ::AI::A_Move)
                                   ? BossActionState::Move
                                   : BossActionState::None;

        // --- ステートごとの処理 ---
        if (bossDef.currentState == BossActionState::Move)
        {
            ProcessMove(bossDef, trans, playerPos, dt);
        }
        else
        {
            ProcessIdle(bossDef, trans, playerPos, dt);
        }

        // --- 速度の積分（共通）---
        trans.position = trans.position + bossDef.moveDirection * bossDef.currentMoveSpeed * dt;
        trans.isDirty = true;
    });
}


// ============================================================================
// デバッグUI：ボスの物理パラメータ調整・現在状態確認・位置ワープ
// ============================================================================
void BossActionSystem::OnGui()
{
    if (!ImGui::CollapsingHeader("Boss Action (Position Debugger)"))
        return;

    auto view = _world->View<TransformComponent, BossActionComponent>();
    if (view.begin() == view.end())
    {
        ImGui::TextDisabled("Boss entity not found in scene.");
        return;
    }

    for (auto e : view)
    {
        auto *trans = _world->GetComponent<TransformComponent>(e);
        if (!trans) continue;

        ImGui::PushID(static_cast<int>(e));
        ImGui::Text("Boss Entity ID: %llu", (unsigned long long)e);

        // 位置を見ながら動かす（デバッグ用ワープ）
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
    const Vector3 &playerPos, float dt)
{
    // 速度をゼロへ滑らかに減速
    bossDef.currentMoveSpeed = (std::max)(0.0f,
        bossDef.currentMoveSpeed - bossDef.deceleration * dt);

    // プレイヤーの方をゆっくり向く（睨みつける演出）
    Vector3 dirToPlayer = playerPos - trans.position;
    dirToPlayer.y       = 0.0f;
    TurnToward(trans, dirToPlayer, bossDef.idleTurnSpeed, dt);
}

void BossActionSystem::ProcessMove(BossActionComponent &bossDef, TransformComponent &trans,
    const Vector3 &playerPos, float dt)
{
    Vector3 dirToPlayer = playerPos - trans.position;
    dirToPlayer.y       = 0.0f;

    if (dirToPlayer.LengthSquared() <= 0.001f)
    {
        bossDef.currentMoveSpeed = 0.0f;
        return;
    }
    dirToPlayer.Normalize();
    bossDef.moveDirection = dirToPlayer;

    // 目標速度（最大速度）へ滑らかに加速
    bossDef.currentMoveSpeed = (std::min)(bossDef.walkSpeed,
        bossDef.currentMoveSpeed + bossDef.acceleration * dt);

    // 進行方向を向く
    TurnToward(trans, dirToPlayer, bossDef.turnSpeed, dt);
}

// BehaviorTreeSystem(L02) の後、物理同期(Push)の前
REGISTER_LOGIC_SYSTEM(BossActionSystem, Priority::LogicStage::L03_PrePhysics);