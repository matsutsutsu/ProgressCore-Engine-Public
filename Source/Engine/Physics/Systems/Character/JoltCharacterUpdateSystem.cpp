#include "JoltCharacterUpdateSystem.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/Common/CCL_TypeInfo.h"
#include "Engine/Physics/JoltIntegration/JoltPhysicsManager.h"
#include "Engine/Physics/JoltIntegration/JoltCharacterContactListener.h"
#include "Engine/Physics/JoltIntegration/PhysicsLayers.h"
#include "Engine/Physics/Components/Jolt/JoltHandleComponent.h"
#include "Engine/Physics/Components/Character/KnockbackComponent.h"
#include <Jolt/Physics/Body/BodyFilter.h>
#include "Engine/Transform/Systems/TransformUpdateSystem.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include <SimpleMath.h>

using namespace DirectX::SimpleMath;
using namespace CCL::ECS;



// テンプレート引数以外に読む型の申告
std::vector<TypeID> JoltCharacterUpdateSystem::GetReadTypes() const
{
    auto types = IfSystem::GetReadTypes();
    types.push_back(TypeInfo<JoltHandleComponent>::ID()); // プロキシ剛体
    return types;
}



// テンプレート引数以外に書く型の申告
std::vector<TypeID> JoltCharacterUpdateSystem::GetWriteTypes() const
{
    auto types = IfSystem::GetWriteTypes();
    types.push_back(TypeInfo<KnockbackComponent>::ID()); // ノックバックと硬直のタイマー
    return types;
}



// 全キャラクターを、移動の依頼に合わせて1フレーム分動かす
void JoltCharacterUpdateSystem::Update(float rawDt)
{
    // 処理フロー:
    // 1. ワープを反映する
    // 2. 上下の速度（重力・ジャンプ）を決める
    // 3. 水平の速度（ルートモーション・速度の上書き・歩き）を決め、ノックバックを足す
    // 4. 移動の予約（MoveTo）か位置で動かす依頼（use3DVelocity）があれば、速度を丸ごと置き換える
    // 5. 地形と干渉させて動かし、Transform とプロキシ剛体へ書き戻す

    if (!m_world->HasResource<JoltPhysicsManager *>())
        return;
    auto               &joltManager   = m_world->GetResource<JoltPhysicsManager *>();
    JPH::PhysicsSystem *physicsSystem = joltManager->GetPhysicsSystem();
    const float         gravityY      = physicsSystem->GetGravity().GetY();

    ForEachWithID([&](EntityID                            id,
                      TransformComponent                 &trans,
                      const JoltCharacterHandleComponent &handle,
                      CharacterMovementComponent         &movement,
                      const JoltCharacterConfigComponent &config,
                      const TimeStateComponent           &time) {
        if (!handle.character)
            return;
        JPH::CharacterVirtual *ch = handle.character.GetPtr();

        // 積分にはこのキャラの時計の dt を使う
        // ヒットストップやスローを移動にも効かせる（実時間だとスロー中にダッシュが伸びる）
        float safeDt = CharacterStepDt(time.localDt);

        // プロキシ剛体の追従には実時間の dt を使う
        // 物理の Step は実時間で回るので、スローの dt を渡すとプロキシが飛んでいく
        float moveDt     = CharacterStepDt(rawDt);
        bool  isGrounded = (ch->GetGroundState() == JPH::CharacterVirtual::EGroundState::OnGround);

        // ---------------------------------------------------------
        // 1. ワープ
        // ---------------------------------------------------------

        ApplyTeleportIfNeeded(trans, ch);

        // ---------------------------------------------------------
        // 2. 上下の速度
        // ---------------------------------------------------------

        float currentVy = ch->GetLinearVelocity().GetY();
        float newVy     = CalculateVerticalVelocity(safeDt, currentVy, config, movement, isGrounded, gravityY);

        // ---------------------------------------------------------
        // 3. 水平の速度
        // ---------------------------------------------------------

        Vector3 newHorizontalVel = CalculateHorizontalVelocity(safeDt, config, movement);
        ApplyKnockback(id, safeDt, newHorizontalVel);

        // ---------------------------------------------------------
        // 4. 位置で動かす依頼
        // ---------------------------------------------------------

        JPH::Vec3 finalVel(newHorizontalVel.x, newVy, newHorizontalVel.z);

        // 移動の予約があれば、このフレームに居るべき位置へ届く速度を依頼にする
        ApplyMoveReservation(movement, ch, time.localDt);

        // 位置で動かす技（跳躍・突進など）の速度で、丸ごと置き換える
        // 重力・ジャンプ・ノックバックも効かない。1フレーム限りの依頼なので読んだら戻す
        if (movement.use3DVelocity)
        {
            finalVel                 = JPH::Vec3(movement.velocity3D.x, movement.velocity3D.y, movement.velocity3D.z);
            movement.currentVelocity = movement.velocity3D; // 依頼が終わった後に慣性を引き継ぐ
            movement.use3DVelocity   = false;
        }

        ch->SetLinearVelocity(finalVel);

        // ---------------------------------------------------------
        // 5. 地形と干渉させて動かす
        // ---------------------------------------------------------

        UpdatePhysicsAndSync(safeDt, moveDt, joltManager, ch, config, trans, id);
    });
}



// ワープした印があれば、CharacterVirtual を Transform の位置・回転へ移す
void JoltCharacterUpdateSystem::ApplyTeleportIfNeeded(TransformComponent &trans, JPH::CharacterVirtual *ch)
{
    if (trans.isTeleported)
    {
        // Jolt にはワールド座標を渡す
        // trans.position は親からの相対なので、親を持つキャラでずれる
        const DirectX::XMFLOAT3 worldPos = trans.GetWorldPosition();
        const DirectX::XMFLOAT4 worldRot = trans.GetWorldRotation();
        ch->SetPosition(JPH::RVec3(worldPos.x, worldPos.y, worldPos.z));
        ch->SetRotation(JPH::Quat(worldRot.x, worldRot.y, worldRot.z, worldRot.w).Normalized());
        trans.isTeleported = false;
    }
}



// 重力とジャンプから、新しい上下の速度を出す
float JoltCharacterUpdateSystem::CalculateVerticalVelocity(float safeDt, float currentVy, const JoltCharacterConfigComponent &config, CharacterMovementComponent &movement, bool isGrounded, float gravityY)
{
    float vy = currentVy;
    if (isGrounded && vy <= 0.0f) vy = 0.0f;

    // ジャンプが決まったフレームだけ、初速を与える
    // 読んだらここで戻すので、依頼した側は戻さなくてよい
    if (movement.jumpImpulse)
    {
        movement.jumpImpulse = false;
        vy                   = config.jumpSpeed;
        isGrounded           = false; // ジャンプした瞬間は空中扱いにする
    }

    // 空中のときだけ重力を掛ける
    if (!isGrounded)
    {
        // 落下中は重力を強める
        // 上昇より落下を速くして、着地を締める
        float currentGravityScale = config.gravityScale;
        if (vy < 0.0f)
        {
            currentGravityScale = config.fallGravityScale;
        }

        // ジャンプの頂点付近は重力を弱め、少し浮かせる
        if (std::abs(vy) < config.apexVelocityThreshold)
        {
            currentGravityScale = config.apexGravityScale;
        }

        vy += gravityY * currentGravityScale * safeDt;
    }

    return vy;
}



// 目標の水平速度へ、慣性付きで寄せる
DirectX::SimpleMath::Vector3 JoltCharacterUpdateSystem::CalculateHorizontalVelocity(float safeDt, const JoltCharacterConfigComponent &config, CharacterMovementComponent &movement)
{
    // 速度の上書きを読み、すぐ既定値へ戻す
    // 依頼した側は効かせたいフレームに毎回書くだけでよく、戻し忘れが起きない
    const float requestedSpeed = movement.overrideSpeed;
    movement.overrideSpeed     = -1.0f;

    // ルートモーション中は、慣性を付けずにアニメの速度をそのまま使う
    if (movement.isRootMotionActive)
    {
        movement.currentVelocity = movement.rootMotionVelocity;
        return movement.currentVelocity;
    }

    // 目標の速さを決める。上書きがあればその速さ、無ければ歩く速さ
    // スティックの傾き（moveIntensity）は歩きにだけ掛ける。上書きは常に全開で出す
    float targetSpeed = config.walkSpeed;

    if (requestedSpeed >= 0.0f)
    {
        targetSpeed = requestedSpeed;
    }
    else
    {
        targetSpeed *= std::clamp(movement.moveIntensity, 0.0f, 1.0f);
    }

    // 速度倍率（チェイン段数による移動速度上昇など）を掛ける
    // 負の値は 1 として扱う
    targetSpeed *= (movement.speedMultiplier > 0.0f) ? movement.speedMultiplier : 1.0f;

    Vector3 targetVelocity = movement.desiredVelocity * targetSpeed;

    // 速くなるときは加速、遅くなるときは減速の速さで寄せる
    // 「すぐ最高速に乗る」と「狙ったぶんだけ滑って止まる」を別々に調整できる
    const float curSpeed = movement.currentVelocity.Length();
    const float tgtSpeed = targetVelocity.Length();
    const float rate     = (tgtSpeed >= curSpeed) ? config.acceleration : config.deceleration;

    // 寄せる割合を 1 までに収める
    // 1 を超えると目標を通り越し、加減速を上げたときに発振する
    const float t            = (std::min)(rate * safeDt, 1.0f);
    movement.currentVelocity = Vector3::Lerp(movement.currentVelocity, targetVelocity, t);

    return movement.currentVelocity;
}



// ノックバックを水平速度へ足し、ノックバックと硬直のタイマーを減らす
void JoltCharacterUpdateSystem::ApplyKnockback(EntityID id, float safeDt, Vector3 &horizontalVel)
{
    auto *kb = m_world->GetComponent<KnockbackComponent>(id);
    if (!kb) return;

    // 命中直後が最大で、残り時間の比で 0 まで弱めて足す
    // CharacterVirtual の速度として動かすので、壁抜けしない
    if (kb->timer > 0.0f)
    {
        const float k = (kb->duration > 0.0f) ? (kb->timer / kb->duration) : 1.0f; // 1→0
        horizontalVel.x += kb->velocity.x * k;
        horizontalVel.z += kb->velocity.z * k;

        kb->timer -= safeDt;
        if (kb->timer <= 0.0f)
        {
            kb->timer    = 0.0f;
            kb->velocity = Vector3::Zero;
        }
    }

    // 操作を受け付けない硬直を減らす
    // このキャラの dt で減らすので、ヒットストップ中は止まる
    if (kb->stunTimer > 0.0f)
    {
        kb->stunTimer -= safeDt;
        if (kb->stunTimer < 0.0f)
            kb->stunTimer = 0.0f;
    }
}



// 移動の予約を1フレーム進め、居るべき位置へ届く速度を依頼にする
void JoltCharacterUpdateSystem::ApplyMoveReservation(CharacterMovementComponent &movement, const JPH::CharacterVirtual *ch, float localDt)
{
    CharacterMoveReservation &r = movement.reservation;

    // 終わった次のフレームは、速度を 0 にして止める
    // 運んだ速度が残ると、着地した後に地面を滑る（続けて予約していれば、ここには来ない）
    if (r.stopNext)
    {
        r.stopNext             = false;
        r.leftover             = 0.0f;
        movement.use3DVelocity = true;
        movement.velocity3D    = Vector3::Zero;
        return;
    }

    if (!r.active) return;

    const JPH::RVec3 p = ch->GetPosition();
    const Vector3    now(static_cast<float>(p.GetX()), static_cast<float>(p.GetY()), static_cast<float>(p.GetZ()));

    // 予約した後の最初のフレームに、今の物理の位置を始点にする
    // Hold は始点そのものに留める
    if (!r.started)
    {
        r.start   = now;
        r.started = true;
        if (r.kind == MoveReservationKind::Hold) r.target = now;
    }

    // 進み具合 u（0=始め, 1=終わり）を出す
    r.elapsed += localDt;
    const float u = (r.duration > 0.0f) ? (std::min)(1.0f, r.elapsed / r.duration) : 1.0f;

    Vector3 v;
    if (r.kind == MoveReservationKind::InDirection)
    {
        // 決めた向きへ、速さを直線で変えながら走る
        v = r.direction * (r.startSpeed + (r.endSpeed - r.startSpeed) * u);
    }
    else
    {
        // 水平は進み方に合わせ、高さは途中で浮かせる
        // 1-(1-u)^2 は u=1 で傾き 0、u^2 は u=0 で傾き 0、4h*u(1-u) は両端で 0
        float eased = u;
        if (r.options.ease == MoveEase::EaseOut) eased = 1.0f - (1.0f - u) * (1.0f - u);
        if (r.options.ease == MoveEase::EaseIn) eased = u * u;

        Vector3 want = Vector3::Lerp(r.start, r.target, eased);
        want.y += 4.0f * r.options.height * u * (1.0f - u);

        // 居るべき位置へ届く速度。積分と同じ dt で割る
        // 上限で丸め、引っかかった遅れを1フレームで取り戻して瞬間移動に見えないようにする
        v = (want - now) / CharacterStepDt(localDt);
        if (v.LengthSquared() > r.options.maxSpeed * r.options.maxSpeed)
        {
            v.Normalize();
            v *= r.options.maxSpeed;
        }
    }

    movement.use3DVelocity = true;
    movement.velocity3D    = v;

    // 終わった。次のフレームに速度を 0 にし、余った時間を次の予約へ渡せるようにする
    if (u >= 1.0f)
    {
        r.active   = false;
        r.stopNext = true;
        r.leftover = (std::max)(0.0f, r.elapsed - r.duration);
    }
}



// 地形と干渉させて動かし、結果を Transform とプロキシ剛体へ書き戻す
void JoltCharacterUpdateSystem::UpdatePhysicsAndSync(float safeDt, float moveDt, JoltPhysicsManager *joltManager, JPH::CharacterVirtual *ch, const JoltCharacterConfigComponent &config, TransformComponent &trans, EntityID id)
{
    JPH::PhysicsSystem         *physicsSystem = joltManager->GetPhysicsSystem();
    JoltHandleComponent        *kinBody       = m_world->GetComponent<JoltHandleComponent>(id); // プロキシ剛体
    JPH::IgnoreSingleBodyFilter bodyFilter(kinBody ? kinBody->bodyID : JPH::BodyID());          // 自分のプロキシは無視

    JPH::CharacterVirtual::ExtendedUpdateSettings updateSettings;
    updateSettings.mWalkStairsStepUp     = JPH::Vec3(0.0f, config.maxStepHeight, 0.0f);
    updateSettings.mStickToFloorStepDown = JPH::Vec3(0.0f, -config.maxStepHeight, 0.0f);

    // 接触をイベントにするリスナーを、動かす間だけ付ける
    // 剛体側と同じバッファへ積み、物理の後でまとめて配送する
    JoltCharacterContactListener contactListener(joltManager->GetContactListener(), id, &physicsSystem->GetBodyInterface());
    ch->SetListener(&contactListener);

    // 地形と干渉させて動かす
    // キャラが世界を見る層は、プロキシ剛体と同じ config.objectLayer
    ch->ExtendedUpdate(
        safeDt, physicsSystem->GetGravity(), updateSettings,
        physicsSystem->GetDefaultBroadPhaseLayerFilter(config.objectLayer),
        physicsSystem->GetDefaultLayerFilter(config.objectLayer),
        bodyFilter, {}, *joltManager->GetTempAllocator());

    // リスナーはスタック変数なので、抜ける前に必ず外す
    ch->SetListener(nullptr);

    // 位置だけを Transform へ書き戻す（向きはゲーム側が書く）
    // CharacterVirtual はワールド座標なので、親を考えてローカルへ直して書く
    JPH::RVec3 pos = ch->GetPosition();
    TransformUpdateSystem::WriteWorldPosition(*m_world, id, trans,
                                              {pos.GetX(), pos.GetY(), pos.GetZ()});

    // プロキシ剛体を CharacterVirtual に追従させる
    // MoveKinematic で動かすと速度が乗り、攻撃判定やレイから正しく見える
    if (kinBody)
    {
        JPH::Quat joltRot = ch->GetRotation();
        joltManager->GetBodyInterface().MoveKinematic(kinBody->bodyID, pos, joltRot, moveDt);
    }
}



REGISTER_LOGIC_SYSTEM(JoltCharacterUpdateSystem, Priority::LogicStage::L03_PrePhysics);
