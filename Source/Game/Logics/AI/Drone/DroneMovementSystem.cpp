// ドローン移動システムの実装（基礎3ステート版）。
//
// FireCharge は内部で stateTimer を使い「タメ→リリース」の2フェーズを持つ
// （旧 LockOn の役割を吸収）。これにより外部からは FireCharge 1つを
// 見ているだけで「予備動作→突撃」という企画上重要な予兆表現ができる。
#include "DroneMovementSystem.h"
#include "Game/Logics/AI/Drone/Squad/SquadState.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/ECS/Core/CCL_World.h"
#include <cmath>

using namespace CCL::ECS;
using namespace DirectX::SimpleMath;

// 予告の震えの位置オフセット。正弦波1本だけで作る。
//
// 1機1軸にする理由:
//   3軸同時に揺らすと、軸ごとに周波数をずらさない限り小さな楕円をなぞるだけになる。
//   ずらすには波を重ねることになり、値が6つ・三角関数1000機×12回／フレームに膨れる。
//   隣同士で揺れる軸が違えば、1本の波でも形は崩れずに表面だけがざわつく。
DirectX::SimpleMath::Vector3
DroneMovementSystem::ShiverOffset(const DroneComponent &drone, float t, float amplitude) const
{
    // 位相を機体ごとにずらす。揃っていると全機が同時に膨らんで「呼吸」に見える。
    const float phase = t * m_shiver.frequency * DirectX::XM_2PI
                      + drone.localIndex * kShiverPhaseStep * m_shiver.scatter;

    const float v = std::sin(phase) * amplitude;

    switch (drone.localIndex % 3)
    {
    case 0:  return {v, 0.0f, 0.0f};
    case 1:  return {0.0f, v, 0.0f};
    default: return {0.0f, 0.0f, v};
    }
}

void DroneMovementSystem::Update(float rawDt)
{
    // 震えの調整値を写し取る。無ければ既定値で作る
    //   （GUI を一度も開かなくても同じ見た目になる）。
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
        // ドローン各自の時計を使用（被弾でヒットストップ等がかかった時にピタッと止まる）
        float dt = time.localDt;

        // ※ 切り払いの吹き飛ばしは Stunned ステートとして下の switch で扱う。
        //    「硬直しているか」の判断（タイマー管理）は DroneBrainSystem の仕事で、
        //    ここは決まった状態を動きに変換するだけに保つ。

        // 位相を進めて 2π で巻き戻す。
        //   放置すると単調増加し、震え(×100)側の実引数が数万ラジアンに達して
        //   float の刻みが粗くなる＝長時間プレイで震えが量子化する。
        drone.hoverTimeOffset += dt;
        if (drone.hoverTimeOffset >= kHoverPhasePeriod)
            drone.hoverTimeOffset -= kHoverPhasePeriod;

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

        // 地面へ潜らせない。全ステート共通で、潜らない保証はここだけが持つ。
        //
        // 技ごとに目標を丸めない理由:
        //   目標を丸めても、吹き飛ばし(Stunned)と剛体追従(RigidFollow)は
        //   targetPosition を経由せずに trans.position へ直接書くので止まらない。
        //   両方に置くと「どちらで止まったのか」が追えなくなるため、積分の直後1か所に集める。
        //
        // 小隊の高度制限（minAltitude）はこれと別物。
        //   あちらは「地上2mを飛ぶ」という飛び方の規則で、埋没防止ではない。
        const float floorY = kDroneGroundY + kDroneGroundClearance;
        if (trans.position.y < floorY)
        {
            trans.position.y = floorY;

            // 下向きの速度を残すと次のフレームも床へ押し付け続け、
            // 得られた高度を毎回吐き出して地面に張り付いたまま滑る。
            if (drone.currentVelocity.y < 0.0f) drone.currentVelocity.y = 0.0f;
        }

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
    const float pGain   = kMoveToTargetGain * SlotLag(drone);
    const float damping = kMoveToTargetDamping;

    Vector3 displacement   = drone.targetPosition - trans.position;
    Vector3 targetVelocity = displacement * pGain;

    const float maxSpeed = drone.moveSpeed * kMoveToTargetSpeedScale;
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
        trans.rotation         = Quaternion::Slerp(trans.rotation, targetRot, (std::min)(kTurnMoveToTarget * dt, 1.0f));
    }
}

inline void DroneMovementSystem::ProcessIdle(TransformComponent &trans, DroneComponent &drone, float dt)
{
    Vector3     displacement   = drone.targetPosition - trans.position;
    const float pGain          = kIdleGain * SlotLag(drone);
    Vector3     targetVelocity = displacement * pGain;

    const float maxSpeed = drone.moveSpeed * kIdleSpeedScale;
    if (targetVelocity.LengthSquared() > maxSpeed * maxSpeed)
    {
        targetVelocity.Normalize();
        targetVelocity *= maxSpeed;
    }

    drone.currentVelocity = Vector3::Lerp(drone.currentVelocity, targetVelocity,
                                          (std::min)(kIdleDamping * dt, 1.0f));

    // ホバー感（フワフワ）。位相は個体ごとに違うので群れが同期して揺れない。
    drone.currentVelocity.y +=
        std::cos(drone.hoverTimeOffset * kHoverFrequency) * kHoverAmplitude * dt;

    // 予告の震え。構えている間だけ、溜まった秒数に応じて強くなる。
    //
    //   windupTimer は「持ち場に着いている／まだ撃つ合図が来ていない」間だけ
    //   DroneFormationSystem が進める。合図が立った瞬間 0 に落ちるので、
    //   震えは撃つのと同時にピタリと止み、そこから技の動きへ入れ替わる。
    //
    //   二乗で立ち上げるのは、序盤をほぼ無音にして「後半になって急に
    //   ざわつき出す」形にするため。線形だと構えた瞬間から一様に震えてしまい、
    //   どこが発生の直前なのかが読み取れない。
    if (drone.windupTimer > 0.0f)
    {
        const float t   = (std::min)(1.0f, drone.windupTimer / m_shiver.rampTime);
        const float amp = m_shiver.amplitude * t * t;

        // 速度ではなく座標を直接動かす。
        //
        //   なぜ速度に足さないか:
        //     速度で揺らすと実際の振れ幅は「振幅 ÷ 角速度」まで縮む。
        //     さらに Idle のバネ(kIdleGain)は低い周波数しか通さないので、
        //     残ったぶんも打ち消される。指定した数字と見た目が一致しない。
        //
        //   なぜ差分（今フレーム − 前フレーム）にするか:
        //     オフセットを毎フレーム足しっぱなしにすると座標へ溜まって
        //     群れごと流れていく。差分なら足した量が次で戻るので溜まらず、
        //     震えを止めた瞬間に持ち場の座標へそのまま収まる。
        //
        //   なぜ時間軸が windupTimer か:
        //     構え始めてからの経過秒なので巻き戻らない。巻き戻る位相を使うと
        //     折り返しで波が途切れないよう周波数を整数に固定する制約が付き、
        //     スライダーで自由に変えられなくなる。
        trans.position = Vector3(trans.position)
                       + ShiverOffset(drone, drone.windupTimer, amp)
                       - ShiverOffset(drone, drone.windupTimer - dt, amp);
    }

    if (drone.targetLookDirection.LengthSquared() > 0.001f)
    {
        float      targetYaw = atan2f(-drone.targetLookDirection.x, -drone.targetLookDirection.z);
        Quaternion targetRot = Quaternion::CreateFromAxisAngle(Vector3::UnitY, targetYaw);
        trans.rotation       = Quaternion::Slerp(trans.rotation, targetRot,
                                                 (std::min)(kTurnIdle * dt, 1.0f));
    }
}

// 突撃。stateTimer > 0 の間は「タメ」（旧LockOnを吸収）、
//        0になった瞬間から「リリース」（高速直進）に切り替わる2フェーズ構造。
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
            trans.rotation         = Quaternion::Slerp(trans.rotation, targetRot,
                                                       (std::min)(kTurnMoveToTarget * dt, 1.0f));
        }
        return;
    }

    // ------------------------------------------------------------
    // [dashモード：発射待ち] fireDelay>0 の間は自分の順番を待つ。
    //   弾幕(KamikazeSwarm)が localIndex から各機に発射順(fireDelay)を割り振り、
    //   「順番に飛ばす」を表現する。0 になったら下のタメ→リリースへ進む。
    //
    //   待っている間は陣形の指示（targetPosition）に従わせる。
    //   ここで静止させると、Strike の瞬間に輪の回転まで止まり、撃つ機以外は
    //   何もしていない置物になる。陣形が「輪を回しつつ、番が近い機だけ
    //   中心へ寄せる」を書けるのは、ここが動いているからこそ成立する。
    // ------------------------------------------------------------
    if (drone.fireDelay > 0.0f)
    {
        drone.fireDelay -= dt;
        ProcessMoveToTarget(trans, drone, dt);
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
        // [フェーズ1] タメ：中央で一度止まってから、撃つ方向へ加速する区間。
        //
        //   予告は陣形側（中心へ集まる動き）が既に済ませているので、
        //   ここは「止まって、押し出される」だけにする。震えも持ち上げも入れない。
        //   足すと予告が二重になり、どちらを読めばよいのか分からなくなる。
        //
        //   この区間は当たり判定を持たない（DroneHitSystem が stateTimer>0 の
        //   直進系を無害として弾く）。中央に溜まっている機に触れても痛くない。
        // ------------------------------------------------------------
        drone.stateTimer -= dt;

        // 残り時間から進行度を出して 0 → kWindupLungeSpeed へ加速する。
        //   残りが kWindupLungeTime より多い間は 0 のまま＝その場で静止する。
        //   これが「中央で止まる」の実体で、専用のフラグもタイマーも要らない。
        float lunge = 0.0f;
        if (drone.stateTimer < kWindupLungeTime)
            lunge = kWindupLungeSpeed * (1.0f - (std::max)(0.0f, drone.stateTimer) / kWindupLungeTime);

        drone.currentVelocity = drone.fireDirection * lunge;

        float      targetYaw = atan2f(-drone.targetLookDirection.x, -drone.targetLookDirection.z);
        Quaternion targetRot = Quaternion::CreateFromAxisAngle(Vector3::UnitY, targetYaw);
        trans.rotation       = Quaternion::Slerp(trans.rotation, targetRot,
                                                 (std::min)(kTurnWindup * dt, 1.0f));
    }
    else
    {
        // ------------------------------------------------------------
        // [フェーズ2] リリース：高速直進。この間だけ「危険」＝ジャスト回避の対象。
        // ------------------------------------------------------------
        drone.currentVelocity = drone.fireDirection * (drone.moveSpeed * kChargeSpeedScale);

        float      targetYaw = atan2f(-drone.targetLookDirection.x, -drone.targetLookDirection.z);
        Quaternion targetRot = Quaternion::CreateFromAxisAngle(Vector3::UnitY, targetYaw);
        trans.rotation       = Quaternion::Slerp(trans.rotation, targetRot,
                                                 (std::min)(kTurnCharge * dt, 1.0f));
    }
}

// 隊列追従の突入。目標へバネで寄るので、速いほど後ろへ流れ、曲がると遅れて曲がる。
//
// 剛体追従（RigidFollow）との違いがこの技の生死を分ける:
//   剛体は毎フレーム座標へ貼り付けるので隊列が1ミリも歪まない。5機で飛んでも
//   「1枚の板が近づいてくる」ようにしか見えず、追従しているように見えない。
//   バネなら速度に比例して後方へずれ、頭が曲がった分だけ遅れて曲がるため、
//   隊列がしなって「前の機体についていっている」ように見える。
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
        trans.rotation          = Quaternion::Slerp(trans.rotation, targetRot,
                                                    (std::min)(kTurnSoftFollow * dt, 1.0f));
    }
}

// 硬直。吹き飛ばし速度で漂いながら減衰し、制御を失った姿勢を見せる。
//        立て直し中は knockbackVelocity が既に 0 付近なので、自然にその場で止まる。
inline void DroneMovementSystem::ProcessStunned(TransformComponent &trans, DroneComponent &drone, float dt)
{
    // 共通の積分（trans.position += currentVelocity * dt）に乗せる。
    drone.currentVelocity = drone.knockbackVelocity;

    float decay = 1.0f - kKnockbackDecay * dt;
    if (decay < 0.0f) decay = 0.0f;
    drone.knockbackVelocity = drone.knockbackVelocity * decay;

    // 姿勢制御を失っている表現。目標方向へ向き直そうとしない（Slerp を掛けない）のが要点で、
    // 「命令を実行している機体」との違いがここで出る。
    Quaternion tumble = Quaternion::CreateFromAxisAngle(Vector3::UnitY, kStunTumbleRate * dt);
    trans.rotation    = Quaternion::Concatenate(trans.rotation, tumble);
}

// 離脱。突撃の勢いを残したまま上方へ抜け、減速しながら進行方向を向く。
inline void DroneMovementSystem::ProcessReturning(TransformComponent &trans, DroneComponent &drone, float dt)
{
    // まず速度を抜く。
    //   突き抜けた直後は突撃速度がそのまま残っている。ここで減速しきらないと、
    //   次の MoveToTarget が高速のまま隊列へ引き戻すので、勢いよく飛んできた機が
    //   その速さのまま急に向きを変えて帰る絵になる。
    float drag = 1.0f - kBreakawayDrag * dt;
    if (drag < 0.0f) drag = 0.0f;
    drone.currentVelocity = drone.currentVelocity * drag;

    // 浮き上がりは「減速してから」効かせる。
    //   全速のまま上向きの力を加えると、突き抜けた瞬間に軌道が跳ね上がって
    //   刺さらずに逸れたように見える。速度が落ちるほど強く効かせると、
    //   勢いのまま少し進んでから、ふわりと持ち上がる形になる。
    //
    //   減速しきった後もこの状態は続く（離脱時間 kBreakawayTime のあいだ）。
    //   その「ほとんど止まって漂っている時間」が、突撃と帰投のあいだの間になる。
    //   この間ドローンは FireCharge ではないので無害＝反撃してよい時間でもある。
    const float speed = drone.currentVelocity.Length();
    const float fast  = (std::min)(speed / kBreakawayFastSpeed, 1.0f);

    drone.currentVelocity += Vector3::UnitY * (kBreakawayLift * (1.0f - fast) * dt);

    // 実際に進んでいる方向へ機首を向ける（目標ではなく速度を見る＝飛行機的な挙動）。
    Vector3 look = drone.currentVelocity;
    if (look.LengthSquared() > 0.001f)
    {
        look.Normalize();
        float      targetYaw   = atan2f(look.x, -look.z);
        float      targetPitch = asinf(std::clamp(look.y, -1.0f, 1.0f));
        Quaternion targetRot   = Quaternion::CreateFromYawPitchRoll(targetYaw, targetPitch, 0.0f);
        trans.rotation         = Quaternion::Slerp(trans.rotation, targetRot,
                                                   (std::min)(kTurnReturning * dt, 1.0f));
    }
}

// L03_PrePhysics で確定させる（PushSystem より前に trans を仕上げる）
REGISTER_LOGIC_SYSTEM(DroneMovementSystem, Priority::LogicStage::L03_PrePhysics);
