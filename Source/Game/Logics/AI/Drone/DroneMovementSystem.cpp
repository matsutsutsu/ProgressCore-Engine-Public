/**
 * ドローン移動システムの実装（基礎3ステート版）。
 *
 * FireCharge は内部で stateTimer を使い「タメ→リリース」の2フェーズを持つ
 * （旧 LockOn の役割を吸収）。これにより外部からは FireCharge 1つを
 * 見ているだけで「予備動作→突撃」という企画上重要な予兆表現ができる。
 */
#include "DroneMovementSystem.h"
#include "Game/Logics/AI/Drone/Squad/SquadState.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/ECS/Core/CCL_World.h"
#include <cmath>

using namespace CCL::ECS;
using namespace DirectX::SimpleMath;

void DroneMovementSystem::Update(float rawDt)
{
    // 小隊の調整値を写し取る（並列ループの中で resource を引かないため）
    if (m_world->HasResource<SquadRegistry>())
    {
        const auto &tuning = m_world->GetResource<SquadRegistry>().tuning;
        m_followGain       = tuning.followGain;
        m_slotLagPerSlot   = tuning.slotLagPerSlot;
    }

    ForEachParallel([this](TransformComponent &trans, DroneComponent &drone,
                        const TimeStateComponent &time) {
        // ドローン各自の時計を使用（被弾でヒットストップ等がかかった時にピタッと止まる）
        float dt = time.localDt;

        // ※ 切り払いの吹き飛ばしは Stunned ステートとして下の switch で扱う。
        //    「硬直しているか」の判断（タイマー管理）は DroneBrainSystem の仕事で、
        //    ここは決まった状態を動きに変換するだけに保つ。

        // ====================================================================
        // 【リード（Leash）によるスタック救済】ボスから遠すぎたら強制ワープ
        // ====================================================================
        //if (const auto *bossTrans = m_world->GetComponent<TransformComponent>(drone.ownerBossId))
        //{
        //    const float     distToBossSqr = (trans.position - bossTrans->position).LengthSquared();
        //    constexpr float leashRadius   = 140.0f;

        //    if (distToBossSqr > (leashRadius * leashRadius))
        //    {
        //        trans.position        = bossTrans->position + Vector3(0.0f, 15.0f, 0.0f);
        //        drone.currentVelocity = Vector3::Zero;
        //        drone.currentState    = DroneState::MoveToTarget;
        //        trans.isDirty         = true;
        //        trans.isTeleported    = true; // 瞬間移動なので Push 側に teleport と伝える
        //    }
        //}

        drone.hoverTimeOffset += dt;

        // --------------------------------------------------------------
        // 各ステートの「意思決定」：drone.currentVelocity / rotation を更新する
        // --------------------------------------------------------------
        switch (drone.currentState)
        {
        case DroneState::MoveToTarget:
            ProcessMoveToTarget(trans, drone, dt);
            break;
        case DroneState::Idle:
            ProcessIdle(trans, drone, dt);
            break;
        case DroneState::FireCharge:
            ProcessFireCharge(trans, drone, dt);
            break;
        case DroneState::Stunned:
            ProcessStunned(trans, drone, dt);
            break;
        case DroneState::Returning:
            ProcessReturning(trans, drone, dt);
            break;
        }

        // 速度を積分して座標を確定する（Jolt へは Push が同期する）
        trans.position = trans.position + drone.currentVelocity * dt;

        // 回転はクォータニオンのドリフトを毎フレーム正規化してから確定
        DirectX::XMVECTOR quatVec = DirectX::XMLoadFloat4(&trans.rotation);
        quatVec                   = DirectX::XMQuaternionNormalize(quatVec);
        DirectX::XMStoreFloat4(&trans.rotation, quatVec);

        trans.isDirty = true;
    });
}

// ============================================================================
// 各ステートの具体的な「意思決定」ロジック
// ============================================================================

inline void DroneMovementSystem::ProcessMoveToTarget(TransformComponent &trans, DroneComponent &drone, float dt)
{
    float pGain   = 20.0f * SlotLag(drone);
    float damping = 15.0f;

    Vector3 displacement   = drone.targetPosition - trans.position;
    Vector3 targetVelocity = displacement * pGain;

    float maxSpeed = drone.moveSpeed * 2.5f;
    if (targetVelocity.LengthSquared() > maxSpeed * maxSpeed)
    {
        targetVelocity.Normalize();
        targetVelocity *= maxSpeed;
    }

    drone.currentVelocity = Vector3::Lerp(drone.currentVelocity, targetVelocity, (std::min)(damping * dt, 1.0f));

    if (drone.targetLookDirection.LengthSquared() > 0.001f)
    {
        Vector3 lookDir = drone.targetLookDirection;
        lookDir.Normalize();
        float      targetYaw   = atan2f(lookDir.x, -lookDir.z);
        float      targetPitch = asinf(std::clamp(lookDir.y, -1.0f, 1.0f));
        Quaternion targetRot   = Quaternion::CreateFromYawPitchRoll(targetYaw, targetPitch, 0.0f);
        trans.rotation         = Quaternion::Slerp(trans.rotation, targetRot, (std::min)(25.0f * dt, 1.0f));
    }
}

inline void DroneMovementSystem::ProcessIdle(TransformComponent &trans, DroneComponent &drone, float dt)
{
    Vector3 displacement   = drone.targetPosition - trans.position;
    float   pGain          = 10.0f * SlotLag(drone);
    float   damping        = 8.0f;
    Vector3 targetVelocity = displacement * pGain;

    float maxSpeed = drone.moveSpeed * 1.5f;
    if (targetVelocity.LengthSquared() > maxSpeed * maxSpeed)
    {
        targetVelocity.Normalize();
        targetVelocity *= maxSpeed;
    }

    drone.currentVelocity = Vector3::Lerp(drone.currentVelocity, targetVelocity, damping * dt);

    // ホバー感（フワフワ）
    float hoverFrequency = 1.0f;
    float hoverAmplitude = 3.0f;
    drone.currentVelocity.y += std::cos(drone.hoverTimeOffset * hoverFrequency) * hoverAmplitude * dt;

    if (drone.targetLookDirection.LengthSquared() > 0.001f)
    {
        float      targetYaw = atan2f(-drone.targetLookDirection.x, -drone.targetLookDirection.z);
        Quaternion targetRot = Quaternion::CreateFromAxisAngle(Vector3::UnitY, targetYaw);
        trans.rotation       = Quaternion::Slerp(trans.rotation, targetRot, 15.0f * dt);
    }
}

/// 突撃。stateTimer > 0 の間は「タメ」（旧LockOnを吸収）、
///        0になった瞬間から「リリース」（高速直進）に切り替わる2フェーズ構造。
inline void DroneMovementSystem::ProcessFireCharge(TransformComponent &trans, DroneComponent &drone, float dt)
{
    // ------------------------------------------------------------
    // [追従モード] 陣形自体が動く攻撃（縦割りのヒンジ回転など）。
    //   fireDirection 直進ではなく、Formation が毎フレーム動かす targetPosition を
    //   剛体追従する（壁が形を保ったまま回る）。タメ（stateTimer）は使わない。
    // ------------------------------------------------------------
    if (drone.chargeMode == ChargeMode::SoftFollow)
    {
        ProcessChargeSoftFollow(trans, drone, dt);
        return;
    }

    if (drone.chargeMode == ChargeMode::RigidFollow)
    {
        // 今フレームで targetPosition に一致させる＝剛体（壁がたわまない）。
        drone.currentVelocity = Vector3::Zero;
        trans.position        = drone.targetPosition;

        if (drone.targetLookDirection.LengthSquared() > 0.001f)
        {
            Vector3 lookDir = drone.targetLookDirection;
            lookDir.Normalize();
            float      targetYaw   = atan2f(lookDir.x, -lookDir.z);
            float      targetPitch = asinf(std::clamp(lookDir.y, -1.0f, 1.0f));
            Quaternion targetRot   = Quaternion::CreateFromYawPitchRoll(targetYaw, targetPitch, 0.0f);
            trans.rotation         = Quaternion::Slerp(trans.rotation, targetRot, (std::min)(25.0f * dt, 1.0f));
        }
        return;
    }

    // ------------------------------------------------------------
    // [dashモード：発射待ち] fireDelay>0 の間はその場で静止して自分の順番を待つ。
    //   弾幕(KamikazeSwarm)が localIndex から各機に発射順(fireDelay)を割り振り、
    //   「順番に飛ばす」を表現する。0 になったら下のタメ→リリースへ進む。
    // ------------------------------------------------------------
    if (drone.fireDelay > 0.0f)
    {
        drone.fireDelay -= dt;
        drone.currentVelocity = Vector3::Zero;

        // 待機中も狙い（発射方向）を向いておく＝溜めている見た目
        if (drone.targetLookDirection.LengthSquared() > 0.001f)
        {
            float      targetYaw = atan2f(-drone.targetLookDirection.x, -drone.targetLookDirection.z);
            Quaternion targetRot = Quaternion::CreateFromAxisAngle(Vector3::UnitY, targetYaw);
            trans.rotation       = Quaternion::Slerp(trans.rotation, targetRot, (std::min)(15.0f * dt, 1.0f));
        }
        return;
    }

    if (drone.fireDirection.LengthSquared() <= 0.001f)
    {
        drone.currentVelocity = Vector3::Zero;
        return;
    }

    if (drone.stateTimer > 0.0f)
    {
        // ------------------------------------------------------------
        // [フェーズ1] タメ（予備動作）：後ろに引き絞りつつ震える
        //   ジャスト回避で狙う「読ませ」の時間帯。
        // ------------------------------------------------------------
        drone.stateTimer -= dt;

        float drawbackSpeed   = 2.0f;
        float upwardLiftSpeed = 4.0f;
        float jitterAmplitude = 5.5f;
        float jitterSpeed     = 100.0f;

        drone.currentVelocity = (drone.fireDirection * drawbackSpeed) + (Vector3::UnitY * upwardLiftSpeed);
        drone.currentVelocity.x += std::cos(drone.hoverTimeOffset * jitterSpeed) * jitterAmplitude;
        drone.currentVelocity.y += std::sin(drone.hoverTimeOffset * (jitterSpeed * 1.2f)) * jitterAmplitude;
        drone.currentVelocity.z += std::cos(drone.hoverTimeOffset * (jitterSpeed * 0.8f)) * jitterAmplitude;

        float      targetYaw = atan2f(-drone.targetLookDirection.x, -drone.targetLookDirection.z);
        Quaternion targetRot = Quaternion::CreateFromAxisAngle(Vector3::UnitY, targetYaw);
        trans.rotation       = Quaternion::Slerp(trans.rotation, targetRot, 20.0f * dt);
    }
    else
    {
        // ------------------------------------------------------------
        // [フェーズ2] リリース：高速直進。この間だけ「危険」＝ジャスト回避の対象。
        // ------------------------------------------------------------
        float chargeSpeedMultiplier = 2.5f;
        drone.currentVelocity       = drone.fireDirection * (drone.moveSpeed * chargeSpeedMultiplier);

        float      targetYaw = atan2f(-drone.targetLookDirection.x, -drone.targetLookDirection.z);
        Quaternion targetRot = Quaternion::CreateFromAxisAngle(Vector3::UnitY, targetYaw);
        trans.rotation       = Quaternion::Slerp(trans.rotation, targetRot, 25.0f * dt);
    }
}

/// 隊列追従の突入。目標へバネで寄るので、速いほど後ろへ流れ、曲がると遅れて曲がる。
///
/// 剛体追従（RigidFollow）との違いがこの技の生死を分ける:
///   剛体は毎フレーム座標へ貼り付けるので隊列が1ミリも歪まない。5機で飛んでも
///   「1枚の板が近づいてくる」ようにしか見えず、追従しているように見えない。
///   バネなら速度に比例して後方へずれ、頭が曲がった分だけ遅れて曲がるため、
///   隊列がしなって「前の機体についていっている」ように見える。
inline void DroneMovementSystem::ProcessChargeSoftFollow(TransformComponent &trans, DroneComponent &drone, float dt)
{
    // 後方の機ほどバネを弱める＝遅れが増える＝しなりが大きくなる
    const float pGain = m_followGain * SlotLag(drone);

    // 隊列全体の速度（先渡し）＋ 目標からのずれの補正（バネ）。
    //   先渡しがあるぶんバネは小さくてよく、間隔を保ったまま柔らかく追従できる。
    Vector3 displacement   = drone.targetPosition - trans.position;
    Vector3 targetVelocity = drone.followFeedForward + displacement * pGain;

    // 頭の突入速度を追い越せる上限にしておく（低いと千切れて置いていかれる）
    const float maxSpeed = drone.moveSpeed * kSoftFollowMaxSpeedScale;
    if (targetVelocity.LengthSquared() > maxSpeed * maxSpeed)
    {
        targetVelocity.Normalize();
        targetVelocity *= maxSpeed;
    }

    drone.currentVelocity = Vector3::Lerp(drone.currentVelocity, targetVelocity,
                                          (std::min)(kSoftFollowDamping * dt, 1.0f));

    // 機首は「目標」ではなく「実際に進んでいる方向」へ向ける。
    //   遅れて曲がる機体は機首も遅れて回るので、隊列のしなりが姿勢にも出る。
    Vector3 look = drone.currentVelocity;
    if (look.LengthSquared() > 0.01f)
    {
        look.Normalize();
        const float targetYaw   = atan2f(look.x, -look.z);
        const float targetPitch = asinf(std::clamp(look.y, -1.0f, 1.0f));
        Quaternion  targetRot   = Quaternion::CreateFromYawPitchRoll(targetYaw, targetPitch, 0.0f);
        trans.rotation          = Quaternion::Slerp(trans.rotation, targetRot, (std::min)(12.0f * dt, 1.0f));
    }
}

/// 硬直。吹き飛ばし速度で漂いながら減衰し、制御を失った姿勢を見せる。
///        立て直し中は knockbackVelocity が既に 0 付近なので、自然にその場で止まる。
inline void DroneMovementSystem::ProcessStunned(TransformComponent &trans, DroneComponent &drone, float dt)
{
    // 共通の積分（trans.position += currentVelocity * dt）に乗せる。
    drone.currentVelocity = drone.knockbackVelocity;

    float decay = 1.0f - kKnockbackDecay * dt;
    if (decay < 0.0f) decay = 0.0f;
    drone.knockbackVelocity = drone.knockbackVelocity * decay;

    // 姿勢制御を失っている表現。目標方向へ向き直そうとしない（Slerp を掛けない）のが要点で、
    // 「命令を実行している機体」との違いがここで出る。
    Quaternion tumble = Quaternion::CreateFromAxisAngle(Vector3::UnitY, 4.0f * dt);
    trans.rotation    = Quaternion::Concatenate(trans.rotation, tumble);
}

/// 離脱。突撃の勢いを残したまま上方へ抜け、減速しながら進行方向を向く。
inline void DroneMovementSystem::ProcessReturning(TransformComponent &trans, DroneComponent &drone, float dt)
{
    drone.currentVelocity += Vector3::UnitY * (kBreakawayLift * dt);

    float drag = 1.0f - kBreakawayDrag * dt;
    if (drag < 0.0f) drag = 0.0f;
    drone.currentVelocity = drone.currentVelocity * drag;

    // 実際に進んでいる方向へ機首を向ける（目標ではなく速度を見る＝飛行機的な挙動）。
    Vector3 look = drone.currentVelocity;
    if (look.LengthSquared() > 0.001f)
    {
        look.Normalize();
        float      targetYaw   = atan2f(look.x, -look.z);
        float      targetPitch = asinf(std::clamp(look.y, -1.0f, 1.0f));
        Quaternion targetRot   = Quaternion::CreateFromYawPitchRoll(targetYaw, targetPitch, 0.0f);
        trans.rotation         = Quaternion::Slerp(trans.rotation, targetRot, (std::min)(8.0f * dt, 1.0f));
    }
}

// L03_PrePhysics で確定させる（PushSystem より前に trans を仕上げる）
REGISTER_LOGIC_SYSTEM(DroneMovementSystem, Priority::LogicStage::L03_PrePhysics);