#include "DroneMovementSystem.h"
#include "Game/Logics/AI/Drone/Squad/SquadState.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/ECS/Core/CCL_World.h"
#include <cmath>

using namespace CCL::ECS;
using namespace DirectX::SimpleMath;

// 予告の震えの位置オフセット。正弦波1本だけで作る。

// 3軸同時には揺らさない。
// 軸ごとに周波数をずらさない限り小さな楕円をなぞるだけで、ずらすには波を重ねる必要があり、
// 三角関数が 1000機 × 12回／フレームに膨れる。

// 揺らす軸は隣同士で変える（localIndex % 3）。
// 1本の波でも、群れの表面だけがざわついて見える。
DirectX::SimpleMath::Vector3
DroneMovementSystem::ShiverOffset(const DroneComponent &drone, float t, float amplitude) const
{
    // 位相を機体ごとにずらす。揃っていると全機が同時に膨らんで「呼吸」に見える。
    const float phase = t * m_shiver.frequency * DirectX::XM_2PI + drone.localIndex * kShiverPhaseStep * m_shiver.scatter;

    const float v = std::sin(phase) * amplitude;

    switch (drone.localIndex % 3)
    {
    case 0:
        return {v, 0.0f, 0.0f};
    case 1:
        return {0.0f, v, 0.0f};
    default:
        return {0.0f, 0.0f, v};
    }
}

// 進行方向 → 姿勢。式の根拠はヘッダのコメントを参照（前方 -Z なので atan2(-x,-z)）。
DirectX::SimpleMath::Quaternion
DroneMovementSystem::LookToRotation(const DirectX::SimpleMath::Vector3 &look, bool includePitch) const
{
    const float yaw = atan2f(-look.x, -look.z);

    if (!includePitch)
        return Quaternion::CreateFromAxisAngle(Vector3::UnitY, yaw);

    const float pitch = asinf(std::clamp(look.y, -1.0f, 1.0f));
    return Quaternion::CreateFromYawPitchRoll(yaw, pitch, 0.0f);
}

// ---------------------------------------------------------------------------
// 入口
// ---------------------------------------------------------------------------

void DroneMovementSystem::Update(float rawDt)
{
    // 処理フロー:
    // 1. 調整値を写し取る
    // 2. ホバーの位相を進める
    // 3. 状態ごとに速度と姿勢を決める
    // 4. 速度を位置へ積分する
    // 5. 床より下へ潜らせない
    // 6. 姿勢を正規化して確定する

    // --- 調整値 ---

    // 震えの調整値を写し取る。無ければ既定値で作る（GUI を開かなくても同じ見た目になる）。
    if (!m_world->HasResource<DroneShiverTuning>())
        m_world->AddResource<DroneShiverTuning>();
    m_shiver = m_world->GetResource<DroneShiverTuning>();

    // 小隊の調整値を写し取る（並列ループの中で resource を引かないため）
    if (m_world->HasResource<SquadRegistry>())
    {
        const auto &tuning = m_world->GetResource<SquadRegistry>().tuning;
        m_followGain       = tuning.followGain;
        m_slotLagPerSlot   = tuning.slotLagPerSlot;
    }

    ForEachParallel([this](TransformComponent &trans, DroneComponent &drone,
                           const TimeStateComponent &time) {
        float dt = time.localDt; // 各機の時間。ヒットストップ中は 0 になり止まる

        // --- ホバーの位相 ---

        // 位相を進めて 2π で巻き戻す。
        // 放置すると単調増加して float の刻みが粗くなり、長時間プレイで揺れが量子化する。
        drone.hoverTimeOffset += dt;
        if (drone.hoverTimeOffset >= kHoverPhasePeriod)
            drone.hoverTimeOffset -= kHoverPhasePeriod;

        // --- 状態ごとの速度と姿勢 ---

        // 各ステートは drone.currentVelocity / rotation を更新するだけ。
        // 位置への積分と床の処理は、この後で全ステート共通に1回だけ行う。
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

        // --- 積分 ---

        trans.position = trans.position + drone.currentVelocity * dt;

        // --- 床 ---

        // 地面へ潜らない保証はここ1か所だけが持つ。
        // 技ごとに目標を丸めても、吹き飛ばしは持ち場を経由せず速度で動き、
        // 剛体追従は trans.position へ直接書くので、陣形側のクランプでは止まらない。

        // 小隊の minAltitude は「地上2mを飛ぶ」という飛び方の規則で、この床とは別物。
        const float floorY = kDroneGroundY + kDroneGroundClearance;
        if (trans.position.y < floorY)
        {
            trans.position.y = floorY;

            // 下向きの速度を残すと次のフレームも床へ押し付け、地面に張り付いたまま滑る。
            if (drone.currentVelocity.y < 0.0f) drone.currentVelocity.y = 0.0f;
        }

        // --- 姿勢の確定 ---

        // Slerp と回転の合成を毎フレーム重ねるので、誤差で長さが1からずれていく。
        // 確定の前に毎フレーム正規化する。
        DirectX::XMVECTOR quatVec = DirectX::XMLoadFloat4(&trans.rotation);
        quatVec                   = DirectX::XMQuaternionNormalize(quatVec);
        DirectX::XMStoreFloat4(&trans.rotation, quatVec);

        trans.isDirty = true;
    });
}

// ---------------------------------------------------------------------------
// 状態ごとの移動
// ---------------------------------------------------------------------------

inline void DroneMovementSystem::ProcessMoveToTarget(TransformComponent &trans, DroneComponent &drone, float dt)
{
    const float pGain   = kMoveToTargetGain * SlotLag(drone);
    const float damping = kMoveToTargetDamping;

    Vector3 displacement   = drone.targetPosition - trans.position;
    Vector3 targetVelocity = displacement * pGain;

    // 上限を掛けるのはバネ（持ち場とのずれを直す分）だけ。先渡しの速度は上限の外で足す。
    // 先渡しまで丸めると、ボスが上限より速く動いたときに置いていかれる。
    const float maxSpeed = drone.moveSpeed * kMoveToTargetSpeedScale;
    if (targetVelocity.LengthSquared() > maxSpeed * maxSpeed)
    {
        targetVelocity.Normalize();
        targetVelocity *= maxSpeed;
    }
    targetVelocity += drone.followFeedForward;

    drone.currentVelocity = Vector3::Lerp(drone.currentVelocity, targetVelocity, (std::min)(damping * dt, 1.0f));

    if (drone.targetLookDirection.LengthSquared() > 0.001f)
    {
        Vector3 lookDir = drone.targetLookDirection;
        lookDir.Normalize();
        const Quaternion targetRot = LookToRotation(lookDir, /*includePitch*/ true);
        trans.rotation             = Quaternion::Slerp(trans.rotation, targetRot, (std::min)(kTurnMoveToTarget * dt, 1.0f));
    }
}

inline void DroneMovementSystem::ProcessIdle(TransformComponent &trans, DroneComponent &drone, float dt)
{
    // 処理フロー:
    // 1. 弱いバネで持ち場へ寄る速度を決める
    // 2. ホバーの上下揺れを足す
    // 3. 予告の震えを位置に足す
    // 4. 機首を水平に向ける

    // --- バネ追従 ---

    Vector3     displacement   = drone.targetPosition - trans.position;
    const float pGain          = kIdleGain * SlotLag(drone);
    Vector3     targetVelocity = displacement * pGain;

    // 上限はバネの分だけ。先渡しの速度（持ち場ごと動いている分）は上限の外で足す（MoveToTarget と同じ）。
    const float maxSpeed = drone.moveSpeed * kIdleSpeedScale;
    if (targetVelocity.LengthSquared() > maxSpeed * maxSpeed)
    {
        targetVelocity.Normalize();
        targetVelocity *= maxSpeed;
    }
    targetVelocity += drone.followFeedForward;

    drone.currentVelocity = Vector3::Lerp(drone.currentVelocity, targetVelocity,
                                          (std::min)(kIdleDamping * dt, 1.0f));

    // --- ホバーの揺れ ---

    // 位相は個体ごとに違うので、群れが同期して揺れない。
    drone.currentVelocity.y +=
        std::cos(drone.hoverTimeOffset * kHoverFrequency) * kHoverAmplitude * dt;

    // --- 予告の震え ---

    // windupTimer は合図が立った瞬間 0 に落ちるので、震えは撃つのと同時に止む。

    // 振れ幅は二乗で立ち上げる。
    // 後半で急に大きくなる形にするため。線形だと発生の直前が読めない。
    if (drone.windupTimer > 0.0f)
    {
        const float t   = (std::min)(1.0f, drone.windupTimer / m_shiver.rampTime);
        const float amp = m_shiver.amplitude * t * t;

        // 速度ではなく座標を、差分（今フレーム − 前フレーム）で動かす。
        // 速度に足すと、振れ幅が「振幅 ÷ 角速度」まで縮んで指定値と見た目が一致しない。
        // 座標に足しっぱなしにすると、座標へ溜まって群れごと流れていく。
        trans.position = Vector3(trans.position) + ShiverOffset(drone, drone.windupTimer, amp) - ShiverOffset(drone, drone.windupTimer - dt, amp);
    }

    // --- 機首 ---

    if (drone.targetLookDirection.LengthSquared() > 0.001f)
    {
        const Quaternion targetRot = LookToRotation(drone.targetLookDirection, /*includePitch*/ false);
        trans.rotation             = Quaternion::Slerp(trans.rotation, targetRot,
                                                       (std::min)(kTurnIdle * dt, 1.0f));
    }
}

inline void DroneMovementSystem::ProcessFireCharge(TransformComponent &trans, DroneComponent &drone, float dt)
{
    // 処理フロー:
    // 1. 追従系（SoftFollow / RigidFollow）は持ち場を追って終わる
    // 2. 直進系は、発射待ちの間は持ち場を追う
    // 3. タメ: 一度止まってから撃つ方向へ加速する
    // 4. リリース: 撃つ方向へ高速で直進する

    // --- 追従系 ---

    // 追従モードは陣形自体が動く攻撃（縦割りのヒンジ回転など）。
    // 直進せず、Formation が毎フレーム動かす targetPosition を追う。タメ（stateTimer）は使わない。
    if (drone.chargeMode == ChargeMode::SoftFollow)
    {
        ProcessChargeSoftFollow(trans, drone, dt);
        return;
    }

    if (drone.chargeMode == ChargeMode::RigidFollow)
    {
        // 形が動き切ったら剛体をやめ、Idle のバネへ渡して惰性で収める。
        // 閉じ切った座標に固定し続けると、全機が同時に静止して不自然に見えるため。

        // 手を離すのは動きだけで、当たり方は変わらない。
        // 危険な区間かどうかは currentState が持っている（DroneHitSystem）。
        if (drone.attackProgress >= 1.0f)
        {
            ProcessIdle(trans, drone, dt);
            return;
        }

        // 動いている間は targetPosition に一致させる＝剛体（壁がたわまない）。

        // ただし速度はゼロにせず、実際に出ている速さの一部を残しておく。
        // ここを 0 にすると、上の受け渡しで Idle が静止から動き出すことになり、結局は同じ止まり方に戻る。
        Vector3 carried = Vector3::Zero;
        if (dt > 0.0f)
        {
            carried = (drone.targetPosition - Vector3(trans.position)) / dt * kRigidFollowInertia;

            const float maxSpeed = drone.moveSpeed * kRigidFollowInertiaMaxScale;
            if (carried.LengthSquared() > maxSpeed * maxSpeed)
            {
                carried.Normalize();
                carried *= maxSpeed;
            }
        }
        drone.currentVelocity = carried;

        // 共通の積分がこの後 currentVelocity * dt を足すので、その分だけ手前に置く。
        // 足し戻った結果が targetPosition ちょうどになるので、上の倍率や上限をどういじっても形は歪まない。
        trans.position = drone.targetPosition - carried * dt;

        if (drone.targetLookDirection.LengthSquared() > 0.001f)
        {
            Vector3 lookDir = drone.targetLookDirection;
            lookDir.Normalize();
            const Quaternion targetRot = LookToRotation(lookDir, /*includePitch*/ true);
            trans.rotation             = Quaternion::Slerp(trans.rotation, targetRot,
                                                           (std::min)(kTurnMoveToTarget * dt, 1.0f));
        }
        return;
    }

    // --- 発射待ち ---

    // fireDelay > 0 の間は自分の順番を待つ。

    // 待っている間も陣形の指示に従わせる。
    // ここで静止させると Strike の瞬間に輪の回転まで止まり、撃つ機以外が静止する。
    // 陣形が「輪を回しつつ番が近い機だけ中心へ寄せる」と書けるのは、ここが動いているから。
    if (drone.fireDelay > 0.0f)
    {
        drone.fireDelay -= dt;
        ProcessMoveToTarget(trans, drone, dt);
        return;
    }

    // 突撃方向が無ければ飛ばさない（方向は BeginCharge か陣形が書く）。
    if (drone.fireDirection.LengthSquared() <= 0.001f)
    {
        drone.currentVelocity = Vector3::Zero;
        return;
    }

    if (drone.stateTimer > 0.0f)
    {
        // --- タメ ---

        // 中央で一度止まってから撃つ方向へ加速する。

        // ここで震えなどの予告は足さない。
        // 予告は陣形側（中心へ集まる動き）が済ませているので、足すと予告が二重になる。

        // この区間は当たり判定を持たない（DroneHitSystem の IsDangerous）。
        drone.stateTimer -= dt;

        // 残り時間から 0 → kWindupLungeSpeed へ加速する。残りが kWindupLungeTime より
        // 多い間は 0 のまま＝その場で静止する（専用のフラグもタイマーも要らない）。
        float lunge = 0.0f;
        if (drone.stateTimer < kWindupLungeTime)
            lunge = kWindupLungeSpeed * (1.0f - (std::max)(0.0f, drone.stateTimer) / kWindupLungeTime);

        drone.currentVelocity = drone.fireDirection * lunge;

        const Quaternion targetRot = LookToRotation(drone.targetLookDirection, /*includePitch*/ false);
        trans.rotation             = Quaternion::Slerp(trans.rotation, targetRot,
                                                       (std::min)(kTurnWindup * dt, 1.0f));
    }
    else
    {
        // --- リリース ---

        // 高速直進。この間だけ「危険」＝当たり判定が開く。
        drone.currentVelocity = drone.fireDirection * (drone.moveSpeed * kChargeSpeedScale);

        const Quaternion targetRot = LookToRotation(drone.targetLookDirection, /*includePitch*/ false);
        trans.rotation             = Quaternion::Slerp(trans.rotation, targetRot,
                                                       (std::min)(kTurnCharge * dt, 1.0f));
    }
}

inline void DroneMovementSystem::ProcessChargeSoftFollow(TransformComponent &trans, DroneComponent &drone, float dt)
{
    const float pGain = m_followGain * SlotLag(drone); // 後方の機ほど弱める＝しなる

    // 先渡しの速度 ＋ 目標からのずれの補正（バネ）。先渡しがあるぶんバネは小さくてよい。
    Vector3 displacement   = drone.targetPosition - trans.position;
    Vector3 targetVelocity = drone.followFeedForward + displacement * pGain;

    // アンカーの突入速度を追い越せる上限にする（低いと隊列から離れて置いていかれる）。
    const float maxSpeed = drone.moveSpeed * kSoftFollowMaxSpeedScale;
    if (targetVelocity.LengthSquared() > maxSpeed * maxSpeed)
    {
        targetVelocity.Normalize();
        targetVelocity *= maxSpeed;
    }

    drone.currentVelocity = Vector3::Lerp(drone.currentVelocity, targetVelocity,
                                          (std::min)(kSoftFollowDamping * dt, 1.0f));

    // 機首は目標ではなく実際の進行方向へ向ける。隊列のしなりが姿勢にも出る。
    Vector3 look = drone.currentVelocity;
    if (look.LengthSquared() > 0.01f)
    {
        look.Normalize();
        const Quaternion targetRot = LookToRotation(look, /*includePitch*/ true);
        trans.rotation             = Quaternion::Slerp(trans.rotation, targetRot,
                                                       (std::min)(kTurnSoftFollow * dt, 1.0f));
    }
}

inline void DroneMovementSystem::ProcessStunned(TransformComponent &trans, DroneComponent &drone, float dt)
{
    drone.currentVelocity = drone.knockbackVelocity;

    float decay = 1.0f - kKnockbackDecay * dt;
    if (decay < 0.0f) decay = 0.0f;
    drone.knockbackVelocity = drone.knockbackVelocity * decay;

    // 目標方向へ向き直そうとしない（Slerp を掛けない）のが要点。「命令を実行している
    // 機体」との違いがここで出る。
    Quaternion tumble = Quaternion::CreateFromAxisAngle(Vector3::UnitY, kStunTumbleRate * dt);
    trans.rotation    = Quaternion::Concatenate(trans.rotation, tumble);
}

inline void DroneMovementSystem::ProcessReturning(TransformComponent &trans, DroneComponent &drone, float dt)
{
    // 処理フロー:
    // 1. 速度を抜く
    // 2. 遅くなるほど上へ浮かせる
    // 3. 機首を進行方向へ向ける

    // --- 減速 ---

    // まず速度を抜く。
    // 減速しきらないと次の MoveToTarget が高速のまま隊列へ引き戻し、
    // 飛んできた機がその速さのまま急に向きを変えて帰る絵になる。
    float drag = 1.0f - kBreakawayDrag * dt;
    if (drag < 0.0f) drag = 0.0f;
    drone.currentVelocity = drone.currentVelocity * drag;

    // --- 浮き上がり ---

    // 浮き上がりは「減速してから」効かせる。
    // 全速のまま上向きの力を加えると、突き抜けた瞬間に軌道が跳ね上がる。

    // 減速しきった後の「漂っている時間」が、突撃と帰投のあいだの間になる。
    // FireCharge ではないので無害で、プレイヤーが反撃してよい時間でもある。
    const float speed = drone.currentVelocity.Length();
    const float fast  = (std::min)(speed / kBreakawayFastSpeed, 1.0f);

    drone.currentVelocity += Vector3::UnitY * (kBreakawayLift * (1.0f - fast) * dt);

    // --- 機首 ---

    // 目標ではなく速度の向きを向く（航空機と同じく進行方向へ機首を向ける）。
    Vector3 look = drone.currentVelocity;
    if (look.LengthSquared() > 0.001f)
    {
        look.Normalize();
        const Quaternion targetRot = LookToRotation(look, /*includePitch*/ true);
        trans.rotation             = Quaternion::Slerp(trans.rotation, targetRot,
                                                       (std::min)(kTurnReturning * dt, 1.0f));
    }
}

// JoltPushSystem（L03_PushToPhysics）が物理へ渡す前に位置を確定する。
REGISTER_LOGIC_SYSTEM(DroneMovementSystem, Priority::LogicStage::L03_PrePhysics);
