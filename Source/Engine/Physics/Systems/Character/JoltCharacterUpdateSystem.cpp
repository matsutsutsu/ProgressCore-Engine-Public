#include "JoltCharacterUpdateSystem.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/Physics/JoltIntegration/JoltPhysicsManager.h"
#include "Engine/Physics/JoltIntegration/JoltCharacterContactListener.h"
#include "Engine/Physics/Components/Jolt/JoltHandleComponent.h"
#include <Jolt/Physics/Body/BodyFilter.h>
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include <SimpleMath.h>

#include "Engine/Platform/Core/Logger.h"


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

        // ===============================================================
        // 震えの原因を特定するための XZ軸ベクトルのログ出力
        // ===============================================================
        //JPH::Vec3 actualVel = ch->GetLinearVelocity();

        //// 入力がある、または動いている時だけログを出す
        //if (input.desiredVelocity.LengthSquared() > 0.001f || actualVel.LengthSq() > 0.001f)
        //{
        //    CCL_LOG_INFO(LogCategory::Game,
        //        "[Jolt Char Jitter] ID:%d | 1.Input(%.2f, %.2f) -> 2.Lerp(%.2f, %.2f) -> 3.Jolt(%.2f, %.2f)",
        //        id,
        //        input.desiredVelocity.x, input.desiredVelocity.z, // 1. カメラとキー入力から決まった理想の向き
        //        input.currentVelocity.x, input.currentVelocity.z, // 2. 慣性(Lerp)をかけた後の要求速度
        //        actualVel.GetX(), actualVel.GetZ());              // 3. 地形と干渉した後の現実の速度
        //}

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
        vy = config.jumpSpeed;
        isGrounded = false; // ジャンプした瞬間は空中扱いにする
    }

    // ===============================================================
    //  空中の時のみ重力を適用する
    // ===============================================================
    if (!isGrounded)
    {
        // 非対称重力の適用 (キビキビ着地)
        float currentGravityScale = config.gravityScale;
        if (vy < 0.0f)
        {
            currentGravityScale = config.fallGravityScale;
        }

        // ===============================================================
        //  滞空時間ハック (Apex Hang Time)
        // ===============================================================
        // 空中かつ、Y軸の速度が閾値(-1.5 〜 1.5)の間なら、重力を極端に弱める
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
    // ========================================================
    // 1. ルートモーション中: 慣性(Lerp)を完全に殺し、アニメーションの速度を100%適用する
    // ========================================================
    if (input.isRootMotionActive)
    {
        input.currentVelocity = input.rootMotionVelocity;
        return input.currentVelocity;
    }

    // ========================================================
    // 2. 通常移動 / ダッシュ中: 慣性(Lerp)をかけて心地よい操作感を作る
    // ========================================================
    float targetSpeed = config.walkSpeed;

    // ダッシュ等で速度が上書きされているか (-1.0f より大きいか)
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

    // キャラクターリスナーをインスタンス化してセットする
    // 一つずつ作成する理由は共有にするとマルチスレッドで死ぬから
    JoltCharacterContactListener contactListener(_world, id, &physicsSystem->GetBodyInterface());
    ch->SetListener(&contactListener);

    ch->ExtendedUpdate(
        safeDt, physicsSystem->GetGravity(), updateSettings,
        physicsSystem->GetDefaultBroadPhaseLayerFilter(PhysicsLayers::MOVING),
        physicsSystem->GetDefaultLayerFilter(PhysicsLayers::MOVING),
        bodyFilter, {}, *joltManager->GetTempAllocator());

    // 衝突判定が終わったら安全のためにリスナーを解除しておく
    // 変数定義をその場で行っているため}で消すときに中身をそのままにしておくと
    // CharacterVisualがずっと不要なメモリを保持してしまうからクリアする
    ch->SetListener(nullptr);

    JPH::RVec3 pos = ch->GetPosition();
    trans.position = {pos.GetX(), pos.GetY(), pos.GetZ()};
    trans.isStatic = false;

    if (kinBody)
    {
        // CharacterVirtual が計算した「傾きのない純粋な回転(Yawのみ)」を取得し、物理剛体にセットする
        JPH::Quat joltRot = ch->GetRotation();

        joltManager->GetBodyInterface().SetPositionAndRotation(kinBody->bodyID, pos, joltRot, JPH::EActivation::DontActivate);
    }
}

REGISTER_LOGIC_SYSTEM(JoltCharacterUpdateSystem, Priority::LogicStage::L03_PrePhysics);