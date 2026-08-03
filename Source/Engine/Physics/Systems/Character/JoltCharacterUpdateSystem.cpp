#include "JoltCharacterUpdateSystem.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/Physics/JoltIntegration/JoltPhysicsManager.h"
#include "Engine/Physics/JoltIntegration/JoltCharacterContactListener.h"
#include "Engine/Physics/JoltIntegration/PhysicsLayers.h"
#include "Engine/Physics/Components/Jolt/JoltHandleComponent.h"
#include "Engine/Physics/Components/Character/KnockbackComponent.h"
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
    if (!m_world->HasResource<JoltPhysicsManager *>())
        return;
    auto               &joltManager   = m_world->GetResource<JoltPhysicsManager *>();
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

        // 時間の安全装置（dtクランプ）。
        // rawDt(実時間)ではなく time.localDt(このキャラの実効dt)で積分する。
        //   ヒットストップ/スロー/ポーズといった時間スケールを移動にも一貫して反映させ、
        //   「タイマーは localDt・移動は実時間」の不整合（ジャスト回避スロー中にダッシュが
        //   何倍にも伸びる不具合）を解消する。通常時は localDt==rawDt なので挙動は不変。
        float safeDt     = (std::max)(0.0001f, (std::min)(time.localDt, 0.0333f));
        // presenceプロキシ(Kinematic剛体)の MoveKinematic 用 dt。
        // 物理本体の Step(dt) は常に実時間で回る（JoltStepSystem は time スケールを掛けない）。
        //   MoveKinematic は「dt 秒で目標へ到達する速度」を内部で速度化するため、ここに
        //   safeDt(スロー中は極小) を渡すと、その速度を Step 側が実dtで積分して 10 倍以上
        //   オーバーシュートし、プロキシが無限遠へ飛んで QuadTree の AABB が NaN/Inf 化して
        //   ブロードフェーズがクラッシュする。→ プロキシの追従には実dtを使う。
        float moveDt     = (std::max)(0.0001f, (std::min)(rawDt, 0.0333f));
        bool  isGrounded = (ch->GetGroundState() == JPH::CharacterVirtual::EGroundState::OnGround);

        // 1. ワープ処理の解決
        ApplyTeleportIfNeeded(trans, ch);

        // 2. 鉛直方向（重力とジャンプ）の計算
        float currentVy = ch->GetLinearVelocity().GetY();
        float newVy     = CalculateVerticalVelocity(safeDt, currentVy, config, input, isGrounded, gravityY);

        // 3. 水平方向（慣性ドリフト減速）の計算
        Vector3 newHorizontalVel = CalculateHorizontalVelocity(safeDt, config, input);

        // 3.5 ノックバック（被弾で書き込まれた速度を減衰させつつ水平速度へ加算）。
        //     CharacterVirtual 経由で動かすので壁抜けしない。残り時間比で線形に減衰させ、
        //     命中直後が最大 → 0 へ。KnockbackComponent は被弾側に常設で付ける。
        if (auto *kb = m_world->GetComponent<KnockbackComponent>(id))
        {
            if (kb->timer > 0.0f)
            {
                const float k = (kb->duration > 0.0f) ? (kb->timer / kb->duration) : 1.0f; // 1→0
                newHorizontalVel.x += kb->velocity.x * k;
                newHorizontalVel.z += kb->velocity.z * k;

                kb->timer -= safeDt;
                if (kb->timer <= 0.0f)
                {
                    kb->timer    = 0.0f;
                    kb->velocity = Vector3::Zero;
                }
            }
        }

        JPH::Vec3 finalVel(newHorizontalVel.x, newVy, newHorizontalVel.z);
        ch->SetLinearVelocity(finalVel);

        // 4. 物理更新と Transform / presenceプロキシへの同期
        //    CharacterVirtual の積分は safeDt(スロー反映)、プロキシ追従は moveDt(実時間)。
        UpdatePhysicsAndSync(safeDt, moveDt, joltManager, ch, config, trans, id);
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

    if (input.overrideSpeed >= 0.0f)
    {
        targetSpeed = input.overrideSpeed;
    }

    // アクション層の速度倍率(チェイン段数などによる移動速度上昇)。負値の暴走を防ぐためクランプ。
    targetSpeed *= (input.speedMultiplier > 0.0f) ? input.speedMultiplier : 1.0f;

    Vector3 targetVelocity = input.desiredVelocity * targetSpeed;

    float friction        = 10.0f;
    input.currentVelocity = Vector3::Lerp(input.currentVelocity, targetVelocity, friction * safeDt);

    return input.currentVelocity;
}

void JoltCharacterUpdateSystem::UpdatePhysicsAndSync(float safeDt, float moveDt, JoltPhysicsManager *joltManager, JPH::CharacterVirtual *ch, const JoltCharacterConfigComponent &config, TransformComponent &trans, EntityID id)
{
    JPH::PhysicsSystem         *physicsSystem = joltManager->GetPhysicsSystem();
    JoltHandleComponent        *kinBody       = m_world->GetComponent<JoltHandleComponent>(id); // presenceプロキシ
    JPH::IgnoreSingleBodyFilter bodyFilter(kinBody ? kinBody->bodyID : JPH::BodyID());         // 自分のプロキシは無視

    JPH::CharacterVirtual::ExtendedUpdateSettings updateSettings;
    updateSettings.mWalkStairsStepUp     = JPH::Vec3(0.0f, config.maxStepHeight, 0.0f);
    updateSettings.mStickToFloorStepDown = JPH::Vec3(0.0f, -config.maxStepHeight, 0.0f);

    // キャラクター用の接触リスナー（剛体側と同じバッファへ蓄積）。
    JoltCharacterContactListener contactListener(joltManager->GetContactListener(), id, &physicsSystem->GetBodyInterface());
    ch->SetListener(&contactListener);

    // キャラ自身のレイヤーは config.objectLayer に統一（presenceプロキシと同じ層で世界を見る）
    ch->ExtendedUpdate(
        safeDt, physicsSystem->GetGravity(), updateSettings,
        physicsSystem->GetDefaultBroadPhaseLayerFilter(config.objectLayer),
        physicsSystem->GetDefaultLayerFilter(config.objectLayer),
        bodyFilter, {}, *joltManager->GetTempAllocator());

    // リスナーはスタック変数なので、スコープを抜ける前に必ず外す
    ch->SetListener(nullptr);

    JPH::RVec3 pos = ch->GetPosition();
    trans.position = {pos.GetX(), pos.GetY(), pos.GetZ()};
    trans.isStatic = false;

    // presence プロキシ（Kinematic 剛体）を CharacterVirtual に追従させる。
    //   SetPosition のテレポートではなく MoveKinematic を使うことで、
    //   「dt後に目標へ到達する速度」が乗り、他の動的ボディとの接触に相対速度が反映される。
    //   これで弾/レイ/敵からプレイヤーを正しく検知できるようになる。
    if (kinBody)
    {
        JPH::Quat joltRot = ch->GetRotation();
        // 実時間(moveDt)で追従させる。Step が実dtで速度を積分するため、ここも実dtで
        //   速度化しないとスロー中にオーバーシュートしてプロキシが飛ぶ（QuadTreeクラッシュ）。
        joltManager->GetBodyInterface().MoveKinematic(kinBody->bodyID, pos, joltRot, moveDt);
    }
}

REGISTER_LOGIC_SYSTEM(JoltCharacterUpdateSystem, Priority::LogicStage::L03_PrePhysics);