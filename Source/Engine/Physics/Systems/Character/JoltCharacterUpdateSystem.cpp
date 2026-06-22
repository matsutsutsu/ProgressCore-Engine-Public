#include "JoltCharacterUpdateSystem.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/Physics/JoltIntegration/JoltPhysicsManager.h"
#include "Engine/Physics/Components/Jolt/JoltHandleComponent.h"
#include <Jolt/Physics/Body/BodyFilter.h>
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include <SimpleMath.h>

using namespace DirectX::SimpleMath;
using namespace CCL::ECS;

// ===================================================================================
// メインの Update ループ (可読性を極限まで高めた設計)
// ===================================================================================
void JoltCharacterUpdateSystem::Update(float rawDt)
{
    if (!_world->HasResource<JoltPhysicsManager *>())
        return;
    auto               &joltManager   = _world->GetResource<JoltPhysicsManager *>();
    JPH::PhysicsSystem *physicsSystem = joltManager->GetPhysicsSystem();
    const float         gravityY      = physicsSystem->GetGravity().GetY();

    ForEachWithID([&](EntityID                            id,
                      TransformComponent                 &trans,
                      const JoltCharacterHandleComponent &handle,
                      CharacterMovementInputComponent    &input,
                      const JoltCharacterConfigComponent &config,
                      const TimeStateComponent           &time) {
        if (!handle.character)
            return;
        JPH::CharacterVirtual *ch = handle.character.GetPtr();

        // 時間の安全装置とタイムスケール
        float safeDt     = (std::max)(0.0001f, (std::min)(rawDt, 0.0333f));
        float timeScale  = (rawDt > 0.0001f) ? (time.localDt / rawDt) : 0.0f;
        bool  isGrounded = (ch->GetGroundState() == JPH::CharacterVirtual::EGroundState::OnGround);

        // 1. ワープ処理の解決
        ApplyTeleportIfNeeded(trans, ch);

        // 2. 鉛直方向（重力とジャンプ）の計算
        float currentVy = ch->GetLinearVelocity().GetY();
        float newVy     = CalculateVerticalVelocity(safeDt, currentVy, config, input, isGrounded, gravityY);

        // 3. 水平方向（慣性ドリフト減速）の計算
        Vector3 newHorizontalVel = CalculateHorizontalVelocity(safeDt, config, input);

        // 4. Joltへの速度注入 (TimeScaleの適用)
        JPH::Vec3 finalVel(newHorizontalVel.x * timeScale, newVy * timeScale, newHorizontalVel.z * timeScale);
        ch->SetLinearVelocity(finalVel);

        // 5. 物理空間の更新と同期
        UpdatePhysicsAndSync(safeDt, joltManager, ch, config, trans, id);
    });
}

// ===================================================================================
// ヘルパー関数の実装
// ===================================================================================

void JoltCharacterUpdateSystem::ApplyTeleportIfNeeded(TransformComponent &trans, JPH::CharacterVirtual *ch)
{
    if (trans.isTeleported)
    {
        ch->SetPosition(JPH::RVec3(trans.position.x, trans.position.y, trans.position.z));
        ch->SetRotation(JPH::Quat(trans.rotation.x, trans.rotation.y, trans.rotation.z, trans.rotation.w));
        trans.isTeleported = false;
    }
}

float JoltCharacterUpdateSystem::CalculateVerticalVelocity(float safeDt, float currentVy, const JoltCharacterConfigComponent &config, const CharacterMovementInputComponent &input, bool isGrounded, float gravityY)
{
    float vy = currentVy;
    if (isGrounded && vy < 0.0f)
        vy = 0.0f;

    if (input.jumpRequested)
    {
        vy = config.jumpSpeed;
    }

    // 非対称重力の適用 (キビキビ着地)
    float currentGravityScale = (vy < 0.0f) ? config.fallGravityScale : config.gravityScale;
    vy += gravityY * currentGravityScale * safeDt;

    return vy;
}

Vector3 JoltCharacterUpdateSystem::CalculateHorizontalVelocity(float safeDt, const JoltCharacterConfigComponent &config, CharacterMovementInputComponent &input)
{
    Vector3 targetVelocity(0, 0, 0);

    if (input.overrideSpeed > 0.0f)
    {
        // 【加速時 (ダッシュ中)】: 瞬間的にトップスピードへ
        targetVelocity        = input.desiredVelocity * input.overrideSpeed;
        input.currentVelocity = targetVelocity;
    }
    else
    {
        // 【減速時 (通常時)】: 目標速度へ指数減衰で滑らかに落とす (慣性の引き継ぎ)
        targetVelocity        = input.desiredVelocity * config.walkSpeed;
        float blend           = 1.0f - std::exp(-config.deceleration * safeDt);
        input.currentVelocity = Vector3::Lerp(input.currentVelocity, targetVelocity, blend);
    }

    return input.currentVelocity;
}

void JoltCharacterUpdateSystem::UpdatePhysicsAndSync(float safeDt, JoltPhysicsManager *joltManager, JPH::CharacterVirtual *ch, const JoltCharacterConfigComponent &config, TransformComponent &trans, EntityID id)
{
    JPH::PhysicsSystem         *physicsSystem = joltManager->GetPhysicsSystem();
    JoltHandleComponent        *kinBody       = _world->GetComponent<JoltHandleComponent>(id);
    JPH::IgnoreSingleBodyFilter bodyFilter(kinBody ? kinBody->bodyID : JPH::BodyID());

    JPH::CharacterVirtual::ExtendedUpdateSettings updateSettings;
    updateSettings.mWalkStairsStepUp     = JPH::Vec3(0.0f, config.maxStepHeight, 0.0f);
    updateSettings.mStickToFloorStepDown = JPH::Vec3(0.0f, -config.maxStepHeight, 0.0f);

    ch->ExtendedUpdate(
        safeDt, physicsSystem->GetGravity(), updateSettings,
        physicsSystem->GetDefaultBroadPhaseLayerFilter(PhysicsLayers::MOVING),
        physicsSystem->GetDefaultLayerFilter(PhysicsLayers::MOVING),
        bodyFilter, {}, *joltManager->GetTempAllocator());

    JPH::RVec3 pos = ch->GetPosition();
    trans.position = {pos.GetX(), pos.GetY(), pos.GetZ()};
    trans.isStatic = false;

    if (kinBody)
    {
        JPH::Quat joltRot(trans.rotation.x, trans.rotation.y, trans.rotation.z, trans.rotation.w);
        joltRot = joltRot.Normalized();
        joltManager->GetBodyInterface().SetPositionAndRotation(kinBody->bodyID, pos, joltRot, JPH::EActivation::DontActivate);
    }
}

REGISTER_LOGIC_SYSTEM(JoltCharacterUpdateSystem, Priority::LogicStage::L03_PrePhysics);