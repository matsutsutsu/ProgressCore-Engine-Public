#include "DroneMovementSystem.h"
#include "Game/Logics/AI/Drone/Squad/SquadState.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/ECS/Core/CCL_World.h"
#include <cmath>

using namespace CCL::ECS;
using namespace DirectX::SimpleMath;

namespace
{
    // ============================================================
    // 調整値（状態ごと）
    // ============================================================

    // バネ追従の強さ。Idle と MoveToTarget が同じ形で使う。
    struct SpringTuning
    {
        float gain;       // 持ち場とのずれ 1m あたりの速度 [1/s]
        float damping;    // 今の速度を目標の速度へ寄せる速さ [1/s]
        float speedScale; // バネで出せる速さの上限（moveSpeed 倍）
        float turnRate;   // 機首を向け直す速さ [1/s]
    };

    // 陣形へ戻る（MoveToTarget）。隊列を「直す」局面なので Idle より強い。
    constexpr SpringTuning kMoveToTarget{.gain = 20.0f, .damping = 15.0f, .speedScale = 2.5f, .turnRate = 25.0f};

    // 陣形を保つ（Idle）。強くすると目標に貼り付いてホバーのゆらぎが消える。
    constexpr SpringTuning kIdle{.gain = 10.0f, .damping = 8.0f, .speedScale = 1.5f, .turnRate = 15.0f};

    // ホバーの上下揺れ（Idle）。
    struct HoverTuning
    {
        float frequency; // kHoverPhasePeriod の巻き戻しが成立するよう整数にする
        float amplitude; // 上下揺れの加速度の振幅 [m/s²]
    };
    constexpr HoverTuning kHover{.frequency = 1.0f, .amplitude = 3.0f};

    // 予告の震えの位相を、機体ごとにずらす幅 [rad]。黄金角なので何機いても一様にばらける。
    constexpr float kShiverPhaseStep = 2.39996323f;

    // 直進の突撃（弾幕）。タメ → リリース の2段。
    struct StraightChargeTuning
    {
        float lungeTime;       // タメの後半で加速にかける長さ [秒]
        float lungeSpeed;      // 加速の終速 [m/s]
        float windupTurnRate;  // タメ中に機首を向け直す速さ [1/s]
        float speedScale;      // リリースの速さ（moveSpeed 倍）
        float releaseTurnRate; // リリース中に機首を向け直す速さ [1/s]
    };

    // タメは「静止 → 加速」の2段でジャスト回避を読ませる。
    //
    //     |<------ DroneBrainSystem::kChargeWindup (0.4秒) ------>|
    //     |<-- 静止 (0.2秒) -->|<--   lungeTime (0.2秒)   -->| 射出
    //
    //   静止の長さは、タメ全体から加速の長さを引いた残りで決まる。
    //
    //   中央で一度止まることで「集まった」と「撃った」が別々の出来事として読める。
    //   定速にすると速度の段差が2回でき、動きが途切れて見える。
    //
    // リリースの速さの実体が DroneTuning.h にあるのは、弾幕の陣形も同じ値で
    // 「飛翔時間 = 距離 ÷ 射出速度」を出すため。
    //
    // 上げすぎると擦り抜ける。1フレームの移動距離が当たり半径の2倍を超えると、
    // 跨いで通り過ぎる（半径2.5m・60fps なら 300 m/s が上限の目安）。
    constexpr StraightChargeTuning kStraightCharge{
        .lungeTime       = 0.2f,
        .lungeSpeed      = 20.0f,
        .windupTurnRate  = 20.0f,
        .speedScale      = kDroneChargeSpeedScale,
        .releaseTurnRate = 25.0f,
    };

    // 剛体追従の突撃（縦割り・挟み込み等）。
    struct RigidFollowTuning
    {
        float inertia;         // 形が動いている間の速度を currentVelocity へ残す割合
        float inertiaMaxScale; // 残す速度の上限（moveSpeed 倍）
        float turnRate;        // 機首を向け直す速さ [1/s]
    };

    // 終わり際に惰性を残す。形が動き切ったあとは Idle のバネへ渡す。
    //
    // inertia が 0 だと、刃や腕が閉じ切った瞬間に全機が空中で同時に静止し、不自然に見える。
    //
    // 倍率をいくつにしても剛体そのものは壊れない。
    // 残した速度ぶんを座標から引いてから積分へ渡すので、着地点は targetPosition ちょうどになる。
    //
    // 上限を置くのは、腕の外側の機は回転半径ぶん速度が跳ね上がり、そのまま残すと収まらずに飛んでいくため。
    constexpr RigidFollowTuning kRigidFollow{
        .inertia         = 0.35f,
        .inertiaMaxScale = 0.6f,
        .turnRate        = kMoveToTarget.turnRate, // MoveToTarget と同じ速さで向け直す
    };

    // 小隊の隊列追従（SoftFollow）。バネの強さは SquadTuning から実行中に写し取る（m_followGain）。
    struct SoftFollowTuning
    {
        float damping;       // 今の速度を目標の速度へ寄せる速さ [1/s]
        float maxSpeedScale; // 出せる速さの上限（moveSpeed 倍）
        float turnRate;      // 機首を向け直す速さ [1/s]
    };

    // 上限は、小隊のアンカーの突入速度を上回る必要がある（低いと隊列から離れて置いていかれる）。
    constexpr SoftFollowTuning kSoftFollow{.damping = 10.0f, .maxSpeedScale = 2.4f, .turnRate = 12.0f};

    // 硬直（Stunned）。
    struct StunTuning
    {
        float tumbleRate;     // 吹き飛ばし中の回転 [rad/s]
        float knockbackDecay; // 吹き飛ばし速度の減衰率 [1/s]
    };
    constexpr StunTuning kStun{.tumbleRate = 4.0f, .knockbackDecay = 6.0f};

    // 離脱（Returning）。
    struct BreakawayTuning
    {
        float drag;      // 減速率 [1/s]
        float lift;      // 上方向へ加える加速度 [m/s²]
        float fastSpeed; // 浮き上がりを効かせ始める速さ [m/s]
        float turnRate;  // 機首を向け直す速さ [1/s]
    };

    // 機首の向け直しを他の状態より鈍くしているのは、「制御を取り戻しつつある」表現。
    constexpr BreakawayTuning kBreakaway{.drag = 2.5f, .lift = 18.0f, .fastSpeed = 40.0f, .turnRate = 8.0f};

    // ============================================================
    // 共通の部品
    // ============================================================

    /**
     * @brief 進行方向から姿勢を作る。yaw の式をここ1箇所に閉じ込める。
     *
     * ドローンのモデルは前方が -Z。CreateFromYawPitchRoll は行ベクトルで v·Rx(pitch)·Ry(yaw) の順に掛かるので、
     *   (0,0,-1)·Rx(p) = (0, +sin p, -cos p)        ← pitch = asin(look.y) と一致する
     *   ここへ Ry(θ) = (-cos p·sin θ, sin p, -cos p·cos θ)
     * となり、look と突き合わせると θ = atan2(-x, -z)。
     *
     * +Z 前提の atan2(x, z) や atan2(x, -z) と混ぜると Z軸に対する鏡像になり、状態が切り替わるたびに機首が最大180°反転する。
     * ±Z を向いているときだけ一致するので気づきにくい。
     *
     * @param look         向かせたい方向。includePitch=true のときだけ正規化が要る（asin に渡すため）。
     * @param includePitch 上下の傾きも付けるか。false は Idle のように「水平だけ向かせたい」場面用。
     * @return 機体の姿勢。
     */
    Quaternion LookToRotation(const Vector3 &look, bool includePitch)
    {
        const float yaw = atan2f(-look.x, -look.z);

        if (!includePitch)
            return Quaternion::CreateFromAxisAngle(Vector3::UnitY, yaw);

        const float pitch = asinf(std::clamp(look.y, -1.0f, 1.0f));
        return Quaternion::CreateFromYawPitchRoll(yaw, pitch, 0.0f);
    }

    // 機首を look の向きへ、turnRate の速さで少しずつ向け直す。
    void TurnToward(TransformComponent &trans, const Vector3 &look, bool includePitch, float turnRate, float dt)
    {
        trans.rotation = Quaternion::Slerp(trans.rotation, LookToRotation(look, includePitch),
                                           (std::min)(turnRate * dt, 1.0f));
    }

    /**
     * @brief バネ追従で drone.currentVelocity を決める（Idle と MoveToTarget）。
     *
     * 持ち場とのずれに比例した速度を目標にし、今の速度をそこへ少しずつ寄せる。
     *
     * @param trans   機体の Transform（今の位置を読む）。
     * @param drone   動かす機体。targetPosition と followFeedForward を読み、currentVelocity を書く。
     * @param dt      この機体の経過時間 [秒]。
     * @param tuning  バネの強さ。
     * @param slotLag 小隊の後ろの席ほど弱める係数（SlotLag）。
     */
    void ApplySpring(const TransformComponent &trans, DroneComponent &drone, float dt,
                     const SpringTuning &tuning, float slotLag)
    {
        Vector3 displacement   = drone.targetPosition - trans.position;
        Vector3 targetVelocity = displacement * (tuning.gain * slotLag);

        // 上限を掛けるのはバネ（持ち場とのずれを直す分）だけ。先渡しの速度は上限の外で足す。
        // 先渡しまで丸めると、ボスが上限より速く動いたときに置いていかれる。
        const float maxSpeed = drone.moveSpeed * tuning.speedScale;
        if (targetVelocity.LengthSquared() > maxSpeed * maxSpeed)
        {
            targetVelocity.Normalize();
            targetVelocity *= maxSpeed;
        }
        targetVelocity += drone.followFeedForward;

        drone.currentVelocity = Vector3::Lerp(drone.currentVelocity, targetVelocity,
                                              (std::min)(tuning.damping * dt, 1.0f));
    }
} // namespace

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

float DroneMovementSystem::SlotLag(const DroneComponent &drone) const
{
    const float lag = 1.0f - static_cast<float>(drone.slotInSquad) * m_slotLagPerSlot;
    return (lag < 0.15f) ? 0.15f : lag; // 遅れすぎて隊列から離れないよう下限を置く
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

void DroneMovementSystem::ProcessMoveToTarget(TransformComponent &trans, DroneComponent &drone, float dt)
{
    ApplySpring(trans, drone, dt, kMoveToTarget, SlotLag(drone));

    if (drone.targetLookDirection.LengthSquared() > 0.001f)
    {
        Vector3 lookDir = drone.targetLookDirection;
        lookDir.Normalize();
        TurnToward(trans, lookDir, /*includePitch*/ true, kMoveToTarget.turnRate, dt);
    }
}

void DroneMovementSystem::ProcessIdle(TransformComponent &trans, DroneComponent &drone, float dt)
{
    // 処理フロー:
    // 1. 弱いバネで持ち場へ寄る速度を決める
    // 2. ホバーの上下揺れを足す
    // 3. 予告の震えを位置に足す
    // 4. 機首を水平に向ける

    // --- バネ追従 ---

    ApplySpring(trans, drone, dt, kIdle, SlotLag(drone));

    // --- ホバーの揺れ ---

    // 位相は個体ごとに違うので、群れが同期して揺れない。
    drone.currentVelocity.y +=
        std::cos(drone.hoverTimeOffset * kHover.frequency) * kHover.amplitude * dt;

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
        TurnToward(trans, drone.targetLookDirection, /*includePitch*/ false, kIdle.turnRate, dt);
}

void DroneMovementSystem::ProcessFireCharge(TransformComponent &trans, DroneComponent &drone, float dt)
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
            carried = (drone.targetPosition - Vector3(trans.position)) / dt * kRigidFollow.inertia;

            const float maxSpeed = drone.moveSpeed * kRigidFollow.inertiaMaxScale;
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
            TurnToward(trans, lookDir, /*includePitch*/ true, kRigidFollow.turnRate, dt);
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

        // 残り時間から 0 → lungeSpeed へ加速する。残りが lungeTime より
        // 多い間は 0 のまま＝その場で静止する（専用のフラグもタイマーも要らない）。
        float lunge = 0.0f;
        if (drone.stateTimer < kStraightCharge.lungeTime)
            lunge = kStraightCharge.lungeSpeed * (1.0f - (std::max)(0.0f, drone.stateTimer) / kStraightCharge.lungeTime);

        drone.currentVelocity = drone.fireDirection * lunge;

        TurnToward(trans, drone.targetLookDirection, /*includePitch*/ false, kStraightCharge.windupTurnRate, dt);
    }
    else
    {
        // --- リリース ---

        // 高速直進。この間だけ「危険」＝当たり判定が開く。
        drone.currentVelocity = drone.fireDirection * (drone.moveSpeed * kStraightCharge.speedScale);

        TurnToward(trans, drone.targetLookDirection, /*includePitch*/ false, kStraightCharge.releaseTurnRate, dt);
    }
}

void DroneMovementSystem::ProcessChargeSoftFollow(TransformComponent &trans, DroneComponent &drone, float dt)
{
    const float pGain = m_followGain * SlotLag(drone); // 後方の機ほど弱める＝しなる

    // 先渡しの速度 ＋ 目標からのずれの補正（バネ）。先渡しがあるぶんバネは小さくてよい。
    //
    // Idle / MoveToTarget（ApplySpring）と違い、上限は先渡しの速度ごと掛ける。
    Vector3 displacement   = drone.targetPosition - trans.position;
    Vector3 targetVelocity = drone.followFeedForward + displacement * pGain;

    // アンカーの突入速度を追い越せる上限にする（低いと隊列から離れて置いていかれる）。
    const float maxSpeed = drone.moveSpeed * kSoftFollow.maxSpeedScale;
    if (targetVelocity.LengthSquared() > maxSpeed * maxSpeed)
    {
        targetVelocity.Normalize();
        targetVelocity *= maxSpeed;
    }

    drone.currentVelocity = Vector3::Lerp(drone.currentVelocity, targetVelocity,
                                          (std::min)(kSoftFollow.damping * dt, 1.0f));

    // 機首は目標ではなく実際の進行方向へ向ける。隊列のしなりが姿勢にも出る。
    Vector3 look = drone.currentVelocity;
    if (look.LengthSquared() > 0.01f)
    {
        look.Normalize();
        TurnToward(trans, look, /*includePitch*/ true, kSoftFollow.turnRate, dt);
    }
}

void DroneMovementSystem::ProcessStunned(TransformComponent &trans, DroneComponent &drone, float dt)
{
    drone.currentVelocity = drone.knockbackVelocity;

    float decay = 1.0f - kStun.knockbackDecay * dt;
    if (decay < 0.0f) decay = 0.0f;
    drone.knockbackVelocity = drone.knockbackVelocity * decay;

    // 目標方向へ向き直そうとしない（Slerp を掛けない）のが要点。「命令を実行している
    // 機体」との違いがここで出る。
    Quaternion tumble = Quaternion::CreateFromAxisAngle(Vector3::UnitY, kStun.tumbleRate * dt);
    trans.rotation    = Quaternion::Concatenate(trans.rotation, tumble);
}

void DroneMovementSystem::ProcessReturning(TransformComponent &trans, DroneComponent &drone, float dt)
{
    // 処理フロー:
    // 1. 速度を抜く
    // 2. 遅くなるほど上へ浮かせる
    // 3. 機首を進行方向へ向ける

    // --- 減速 ---

    // まず速度を抜く。
    // 減速しきらないと次の MoveToTarget が高速のまま隊列へ引き戻し、
    // 飛んできた機がその速さのまま急に向きを変えて帰る絵になる。
    float drag = 1.0f - kBreakaway.drag * dt;
    if (drag < 0.0f) drag = 0.0f;
    drone.currentVelocity = drone.currentVelocity * drag;

    // --- 浮き上がり ---

    // 浮き上がりは「減速してから」効かせる。
    // 全速のまま上向きの力を加えると、突き抜けた瞬間に軌道が跳ね上がる。

    // 減速しきった後の「漂っている時間」が、突撃と帰投のあいだの間になる。
    // FireCharge ではないので無害で、プレイヤーが反撃してよい時間でもある。
    const float speed = drone.currentVelocity.Length();
    const float fast  = (std::min)(speed / kBreakaway.fastSpeed, 1.0f);

    drone.currentVelocity += Vector3::UnitY * (kBreakaway.lift * (1.0f - fast) * dt);

    // --- 機首 ---

    // 目標ではなく速度の向きを向く（航空機と同じく進行方向へ機首を向ける）。
    Vector3 look = drone.currentVelocity;
    if (look.LengthSquared() > 0.001f)
    {
        look.Normalize();
        TurnToward(trans, look, /*includePitch*/ true, kBreakaway.turnRate, dt);
    }
}

// JoltPushSystem（L03_PushToPhysics）が物理へ渡す前に位置を確定する。
REGISTER_LOGIC_SYSTEM(DroneMovementSystem, Priority::LogicStage::L03_PrePhysics);
