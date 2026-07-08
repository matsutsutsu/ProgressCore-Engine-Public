#include "JoltCharacterUpdateSystem.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/Physics/JoltIntegration/JoltPhysicsManager.h"
#include "Engine/Physics/JoltIntegration/JoltCharacterContactListener.h"
#include "Engine/Physics/JoltIntegration/PhysicsLayers.h" // PhysicsLayers::MOVING を明示的に参照
#include "Engine/Physics/Components/Jolt/JoltHandleComponent.h"
#include <Jolt/Physics/Body/BodyFilter.h>
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include <SimpleMath.h>

using namespace DirectX::SimpleMath;
using namespace CCL::ECS;

// ===================================================================================
// メインの Update ループ
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

        // 時間の安全装置（dtクランプ）
        float safeDt     = (std::max)(0.0001f, (std::min)(rawDt, 0.0333f));
        bool  isGrounded = (ch->GetGroundState() == JPH::CharacterVirtual::EGroundState::OnGround);

        // 1. ワープ処理の解決
        ApplyTeleportIfNeeded(trans, ch);

        // 2. 鉛直方向（重力とジャンプ）の計算
        float currentVy = ch->GetLinearVelocity().GetY();
        float newVy     = CalculateVerticalVelocity(safeDt, currentVy, config, input, isGrounded, gravityY);

        // 3. 水平方向（慣性ドリフト減速）の計算
        Vector3 newHorizontalVel = CalculateHorizontalVelocity(safeDt, config, input);

        JPH::Vec3 finalVel(newHorizontalVel.x, newVy, newHorizontalVel.z);
        ch->SetLinearVelocity(finalVel);

        // 4. 物理更新と Transform への同期
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
    if (isGrounded && vy <= 0.0f) vy = 0.0f;

    if (input.jumpRequested)
    {
        vy         = config.jumpSpeed;
        isGrounded = false; // ジャンプした瞬間は空中扱いにする
    }

    // 空中の時のみ重力を適用する
    if (!isGrounded)
    {
        // 非対称重力の適用 (キビキビ着地)
        float currentGravityScale = config.gravityScale;
        if (vy < 0.0f)
        {
            currentGravityScale = config.fallGravityScale;
        }

        // 滞空時間ハック (Apex Hang Time)
        // 空中かつ、Y軸の速度が閾値の間なら重力を極端に弱める
        if (std::abs(vy) < config.apexVelocityThreshold)
        {
            currentGravityScale = config.apexGravityScale;
        }

        vy += gravityY * currentGravityScale * safeDt;
    }

    return vy;
}

DirectX::SimpleMath::Vector3 JoltCharacterUpdateSystem::CalculateHorizontalVelocity(float safeDt, const JoltCharacterConfigComponent &config, CharacterMovementInputComponent &input)
{
    // 1. ルートモーション中: 慣性(Lerp)を殺し、アニメーションの速度を100%適用する
    if (input.isRootMotionActive)
    {
        input.currentVelocity = input.rootMotionVelocity;
        return input.currentVelocity;
    }

    // 2. 通常移動 / ダッシュ中: 慣性(Lerp)をかけて心地よい操作感を作る
    float targetSpeed = config.walkSpeed;

    // ダッシュ等で速度が上書きされているか
    if (input.overrideSpeed >= 0.0f)
    {
        targetSpeed = input.overrideSpeed;
    }

    Vector3 targetVelocity = input.desiredVelocity * targetSpeed;

    // プレイヤーの入力に対して心地よい慣性(Lerp)をかける
    float friction        = 10.0f;
    input.currentVelocity = Vector3::Lerp(input.currentVelocity, targetVelocity, friction * safeDt);

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

    // キャラクター用の接触リスナー。
    // ★fix: EventBus 即時発行ではなく、剛体側と同じバッファ(sink)へ蓄積する方式に統一。
    //   マルチスレッド共有を避けるため 1体ずつスタック上に生成する。
    JoltCharacterContactListener contactListener(joltManager->GetContactListener(), id, &physicsSystem->GetBodyInterface());
    ch->SetListener(&contactListener);

    ch->ExtendedUpdate(
        safeDt, physicsSystem->GetGravity(), updateSettings,
        physicsSystem->GetDefaultBroadPhaseLayerFilter(PhysicsLayers::MOVING),
        physicsSystem->GetDefaultLayerFilter(PhysicsLayers::MOVING),
        bodyFilter, {}, *joltManager->GetTempAllocator());

    // リスナーはスタック変数なので、スコープを抜ける前に必ず外す
    // （CharacterVirtual がダングリングポインタを保持しないように）
    ch->SetListener(nullptr);

    JPH::RVec3 pos = ch->GetPosition();
    trans.position = {pos.GetX(), pos.GetY(), pos.GetZ()};
    trans.isStatic = false;

    if (kinBody)
    {
        // CharacterVirtual が計算した回転(Yawのみ)を剛体へ同期する場合はここで行う。
        // 現状は無効化中（剛体追従が必要になったら有効化すること）。
        // JPH::Quat joltRot = ch->GetRotation();
        // joltManager->GetBodyInterface().SetPositionAndRotation(kinBody->bodyID, pos, joltRot, JPH::EActivation::DontActivate);
    }
}

REGISTER_LOGIC_SYSTEM(JoltCharacterUpdateSystem, Priority::LogicStage::L03_PrePhysics);