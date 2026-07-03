#include "JoltCharacterUpdateSystem.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/Physics/JoltIntegration/JoltPhysicsManager.h"
#include "Engine/Physics/JoltIntegration/JoltCharacterContactListener.h"
#include "Engine/Physics/Components/Jolt/JoltHandleComponent.h"
#include <Jolt/Physics/Body/BodyFilter.h>
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include <SimpleMath.h>
#include <chrono> 
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

          // ===============================================================
        // ★ マイクロ秒精度の呼び出し間隔検証（static: エンティティ間で共有される点に注意。
        //   将来的にキャラが複数体になったら id 別の map に変更すること）
        // ===============================================================
        static auto lastCallTime = std::chrono::steady_clock::now();
        auto        nowTime      = std::chrono::steady_clock::now();
        double      intervalUs   = std::chrono::duration<double, std::micro>(nowTime - lastCallTime).count();
        lastCallTime             = nowTime;


        // 時間の安全装置とタイムスケール
        float safeDt     = (std::max)(0.0001f, (std::min)(rawDt, 0.0333f));
        float timeScale  = (rawDt > 0.0001f) ? (time.localDt / rawDt) : 0.0f;
        bool  isGrounded = (ch->GetGroundState() == JPH::CharacterVirtual::EGroundState::OnGround);

         JPH::Vec3 velBeforeSet = ch->GetLinearVelocity(); // Set前の速度も記録


        // 1. ワープ処理の解決
        ApplyTeleportIfNeeded(trans, ch);

        // 2. 鉛直方向（重力とジャンプ）の計算
        float currentVy = ch->GetLinearVelocity().GetY();
        float newVy     = CalculateVerticalVelocity(safeDt, currentVy, config, input, isGrounded, gravityY);

        // 3. 水平方向（慣性ドリフト減速）の計算
        Vector3 newHorizontalVel = CalculateHorizontalVelocity(safeDt, config, input);

        // 検証用: timeScale を速度に掛けない
        JPH::Vec3 finalVel(newHorizontalVel.x, newVy, newHorizontalVel.z);
        ch->SetLinearVelocity(finalVel);

           JPH::RVec3 posBefore = ch->GetPosition();

        UpdatePhysicsAndSync(safeDt, joltManager, ch, config, trans, id);

        JPH::RVec3 posAfter = ch->GetPosition();

        // ===============================================================
        // ★ 統合デバッグログ: 呼び出し間隔 + 入力 + 速度 + 移動量を1行で
        // ===============================================================
     /*   CCL_LOG_INFO(LogCategory::Game,
            "[UnifiedDbg] id=%u intervalUs=%.1f rawDt=%.5f localDt=%.5f "
            "desiredVel(%.3f,%.3f) currentVel(%.3f,%.3f) finalVel(%.3f,%.3f) "
            "dPos(x=%.4f z=%.4f)",
            (uint32_t)id, intervalUs, rawDt, time.localDt,
            input.desiredVelocity.x, input.desiredVelocity.z,
            input.currentVelocity.x, input.currentVelocity.z,
            finalVel.GetX(), finalVel.GetZ(),
            posAfter.GetX() - posBefore.GetX(),
            posAfter.GetZ() - posBefore.GetZ());*/
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

   JPH::RVec3 posBefore = ch->GetPosition();

    ch->ExtendedUpdate(
        safeDt, physicsSystem->GetGravity(), updateSettings,
        physicsSystem->GetDefaultBroadPhaseLayerFilter(PhysicsLayers::MOVING),
        physicsSystem->GetDefaultLayerFilter(PhysicsLayers::MOVING),
        bodyFilter, {}, *joltManager->GetTempAllocator());

    JPH::RVec3 posAfter = ch->GetPosition();
    JPH::Vec3  gN       = ch->GetGroundNormal();
    auto       gState   = ch->GetGroundState();

    //CCL_LOG_INFO(LogCategory::Game,
    //    "[CharDbg] dPos(x=%.4f z=%.4f y=%.4f) gState=%d gN(%.2f,%.2f,%.2f)",
    //    posAfter.GetX() - posBefore.GetX(),
    //    posAfter.GetZ() - posBefore.GetZ(),
    //    posAfter.GetY() - posBefore.GetY(),
    //    (int)gState,
    //    gN.GetX(), gN.GetY(), gN.GetZ());



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

        //joltManager->GetBodyInterface().SetPositionAndRotation(kinBody->bodyID, pos, joltRot, JPH::EActivation::DontActivate);
    }
}

REGISTER_LOGIC_SYSTEM(JoltCharacterUpdateSystem, Priority::LogicStage::L03_PrePhysics);