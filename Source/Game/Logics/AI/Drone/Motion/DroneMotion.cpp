#include "DroneMotion.h"

#include <algorithm>
#include <cmath>

#include "Game/Logics/AI/Drone/Components/DroneTuning.h"
#include "Game/Logics/AI/Drone/Squad/SquadState.h"
#include "Engine/ECS/Core/CCL_World.h"

using namespace DirectX::SimpleMath;

namespace CCL::AI::Motion
{
    namespace
    {
        // ============================================================
        // 調整値
        // ============================================================

        // --- 持ち場へ戻る・保つ ---

        // バネで持ち場へ寄せる強さ
        struct SpringTuning
        {
            float gain;       // 持ち場とのずれ 1m あたりの速度 [1/s]
            float damping;    // 今の速度を目標の速度へ寄せる速さ [1/s]
            float speedScale; // バネで出せる速さの上限（moveSpeed 倍）
            float turnRate;   // 機首を向け直す速さ [1/s]
        };

        // 戻るときは隊列を直す局面なので、保つときより強い。速さの上限は MotionTuning::moveSpeedScale を使う
        constexpr SpringTuning kMoveToTarget{.gain = 20.0f, .damping = 15.0f, .turnRate = 25.0f};

        // 強くすると持ち場に貼り付いて、ホバーの揺れが消える
        constexpr SpringTuning kIdle{.gain = 10.0f, .damping = 8.0f, .speedScale = 1.5f, .turnRate = 15.0f};

        // ホバーの上下揺れ（Idle）
        struct HoverTuning
        {
            float frequency; // 整数にする（kHoverPhasePeriod での巻き戻しと合わせるため）
            float amplitude; // 上下揺れの加速度の振幅 [m/s²]
        };
        constexpr HoverTuning kHover{.frequency = 1.0f, .amplitude = 3.0f};

        // 予告の震え（Idle）
        struct ShiverTuning
        {
            float amplitude; // 溜め切ったときの振れ幅 [m]
            float frequency; // 震えの速さ [Hz]。上げすぎるとノイズに見えて予告にならない
            float rampTime;  // 最大まで溜まる時間 [秒]
            float scatter;   // 機体ごとの位相のずれ具合。0=全機同時 / 1=最大限ずらす
        };
        constexpr ShiverTuning kShiver{.amplitude = 0.25f, .frequency = 7.0f, .rampTime = 1.2f, .scatter = 1.0f};

        // 予告の震えの位相を、機体ごとにずらす幅 [rad]。黄金角なので何機いても一様にばらける
        constexpr float kShiverPhaseStep = 2.39996323f;

        // --- 直進（弾幕） ---

        struct StraightChargeTuning
        {
            float windup;          // タメ全体の長さ [秒]
            float lungeTime;       // タメの後半で加速にかける長さ [秒]
            float lungeSpeed;      // 加速の終速 [m/s]
            float windupTurnRate;  // タメ中に機首を向け直す速さ [1/s]
            float speedScale;      // リリースの速さ（moveSpeed 倍）
            float releaseTurnRate; // リリース中に機首を向け直す速さ [1/s]
        };

        // タメは「静止 → 加速」の2段。中央で一度止めて、「集まった」と「撃った」を別の出来事として読ませる
        //
        //     |<----------------- windup ----------------->|
        //     |<----- 静止 ----->|<----- lungeTime ----->| 射出
        constexpr StraightChargeTuning kStraightCharge{
            .windup          = 0.4f,
            .lungeTime       = 0.2f,
            .lungeSpeed      = 20.0f,
            .windupTurnRate  = 20.0f,
            .speedScale      = kDroneChargeSpeedScale, // 上げすぎると、1フレームで当たり判定を跨いで擦り抜ける
            .releaseTurnRate = 25.0f,
        };

        // --- 剛体追従（データ陣形） ---

        struct RigidFollowTuning
        {
            float inertia;         // 実際に出ている速度のうち、currentVelocity へ残す割合
            float inertiaMaxScale; // 残す速度の上限（moveSpeed 倍）
            float turnRate;        // 機首を向け直す速さ [1/s]
        };

        // 上限は、腕の外側の機が回転半径ぶん速くなり、残すと飛んでいくのを防ぐため
        constexpr RigidFollowTuning kRigidFollow{.inertia = 0.35f, .inertiaMaxScale = 0.6f, .turnRate = 25.0f};

        // --- バネ追従（小隊突撃） ---

        struct SoftFollowTuning
        {
            float damping;       // 今の速度を目標の速度へ寄せる速さ [1/s]
            float maxSpeedScale; // 出せる速さの上限（moveSpeed 倍）。アンカーの突入速度より上にする
            float turnRate;      // 機首を向け直す速さ [1/s]
        };
        constexpr SoftFollowTuning kSoftFollow{.damping = 10.0f, .maxSpeedScale = 2.4f, .turnRate = 12.0f};

        // バネ追従の強さ。BeginMotionFrame だけが書き、並列ループの間は読むだけ
        float gSoftFollowGain = 10.0f;

        // 調整 UI の値。同じく BeginMotionFrame だけが書く
        MotionTuning gTuning{};

        // ============================================================
        // 内部の部品
        // ============================================================

        // kMoveToTarget に、調整 UI の速さの上限を入れたもの
        SpringTuning MoveToTargetSpring()
        {
            SpringTuning spring = kMoveToTarget;
            spring.speedScale   = gTuning.moveSpeedScale;
            return spring;
        }



        /**
         * @brief 持ち場とのずれに比例した速度へ、今の速度を少しずつ寄せる（Idle と MoveToTarget）
         * @param trans  機体の Transform（位置を読む）
         * @param drone  動かす機体。currentVelocity を書く
         * @param dt     この機体の経過時間 [秒]
         * @param tuning バネの強さ。drone.springScale を掛けて使う
         */
        void ApplySpring(const TransformComponent &trans, DroneComponent &drone, float dt,
                         const SpringTuning &tuning)
        {
            Vector3 displacement   = drone.targetPosition - trans.position;
            Vector3 targetVelocity = displacement * (tuning.gain * drone.springScale);

            // 上限はバネの分だけに掛け、先渡しの速度は後で足す
            // 先渡しまで丸めると、ボスが上限より速く動いたときに置いていかれる
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



        /**
         * @brief 予告の震えの位置オフセットを返す。1軸だけを正弦波で揺らす
         *
         * 揺らす軸は隣の機と変える（localIndex % 3）。3軸を同時に揺らすより安く、群れの表面がざわついて見える
         *
         * @param drone     震わせる機体。位相と軸を localIndex から決める
         * @param t         構え始めてからの経過時間 [秒]
         * @param amplitude 振れ幅 [m]
         * @return 位置オフセット [m]
         */
        Vector3 ShiverOffset(const DroneComponent &drone, float t, float amplitude)
        {
            // 位相を機体ごとにずらす。揃っていると全機が同時に膨らんで「呼吸」に見える
            const float phase = t * kShiver.frequency * DirectX::XM_2PI + drone.localIndex * kShiverPhaseStep * kShiver.scatter;

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



        // 直進の突撃（弾幕）。発射待ち → タメ → リリースの順に進む
        void MoveStraight(TransformComponent &trans, DroneComponent &drone, float dt)
        {
            // 処理フロー:
            // 1. 発射待ちの間は持ち場を追う
            // 2. タメ: 一度止まってから撃つ方向へ加速する
            // 3. リリース: 撃つ方向へ高速で直進する

            // --- 発射待ち ---

            // 待つ間も持ち場を追わせる。止めると、撃つ番でない機まで輪の回転ごと止まる
            if (drone.fireDelay > 0.0f)
            {
                drone.fireDelay -= dt;
                MoveToSlot(trans, drone, dt);
                return;
            }

            // 撃つ方向がなければ飛ばさない
            if (drone.fireDirection.LengthSquared() <= 0.001f)
            {
                drone.currentVelocity = Vector3::Zero;
                return;
            }

            if (drone.chargeWindup > 0.0f)
            {
                // --- タメ ---

                // 予告の震えは足さない。中心へ集まる動きで予告済みなので、足すと二重になる
                drone.chargeWindup -= dt;

                // 残りが lungeTime より多い間は 0 のまま＝その場で静止する
                float lunge = 0.0f;
                if (drone.chargeWindup < kStraightCharge.lungeTime)
                    lunge = kStraightCharge.lungeSpeed * (1.0f - (std::max)(0.0f, drone.chargeWindup) / kStraightCharge.lungeTime);

                drone.currentVelocity = drone.fireDirection * lunge;

                TurnToward(trans, drone.targetLookDirection, /*includePitch*/ false, kStraightCharge.windupTurnRate, dt);
            }
            else
            {
                // --- リリース ---

                drone.currentVelocity = drone.fireDirection * (drone.moveSpeed * kStraightCharge.speedScale);

                TurnToward(trans, drone.targetLookDirection, /*includePitch*/ false, kStraightCharge.releaseTurnRate, dt);
            }
        }



        // 剛体追従の突撃（データ陣形）。追いつくまではバネで寄せ、形が動く間は持ち場に一致させ、動き切ったら Idle のバネへ渡す
        void MoveRigidFollow(TransformComponent &trans, DroneComponent &drone, float dt)
        {
            // 処理フロー:
            // 1. 形が動き切っていたら Idle のバネへ渡す
            // 2. まだ持ち場に追いついていなければ、強いバネで寄せる
            // 3. 持ち場に一致させる（剛体）

            // --- 動き切った後 ---

            // 閉じ切った座標に固定し続けると、全機が同時に静止して不自然に見える
            // 手を離すのは動きだけで、当たり判定は FireCharge の間は開いたまま
            if (drone.attackProgress >= 1.0f)
            {
                HoldSlot(trans, drone, dt);
                return;
            }

            // --- 追いつくまで ---

            // 持ち場に着く前に撃つ合図が来た機を、いきなり剛体にすると持ち場へ瞬間移動する
            // 一度追いついたら突撃の終わりまで剛体のままにする（速く動く腕の先で、1フレームの移動が閾値を超えても外れない）
            if (!drone.rigidLocked)
            {
                if (Vector3::Distance(trans.position, drone.targetPosition) > gTuning.rigidCatchUp)
                {
                    MoveToSlot(trans, drone, dt);
                    return;
                }
                drone.rigidLocked = true;
            }

            // --- 剛体 ---

            // 速度は 0 にせず、実際に出ている速さの一部を残す
            // 0 にすると、上の受け渡しで Idle が静止から動き出し、結局同じ止まり方になる
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

            // この後の積分で currentVelocity * dt が足されるので、その分だけ手前に置く
            // 足し戻ると targetPosition ちょうどになるので、kRigidFollow をどう変えても形は歪まない
            trans.position = drone.targetPosition - carried * dt;

            if (drone.targetLookDirection.LengthSquared() > 0.001f)
            {
                Vector3 lookDir = drone.targetLookDirection;
                lookDir.Normalize();
                TurnToward(trans, lookDir, /*includePitch*/ true, kRigidFollow.turnRate, dt);
            }
        }



        // バネ追従の突撃（小隊突撃）。持ち場へバネで寄るので、速いほど後ろへ流れて隊列がしなる
        void MoveSoftFollow(TransformComponent &trans, DroneComponent &drone, float dt)
        {
            // 後ろの席ほど springScale が小さく、遅れて付いてくる
            const float pGain = gSoftFollowGain * drone.springScale;

            // ApplySpring と違い、上限は先渡しの速度ごと掛ける
            Vector3 displacement   = drone.targetPosition - trans.position;
            Vector3 targetVelocity = drone.followFeedForward + displacement * pGain;

            const float maxSpeed = drone.moveSpeed * kSoftFollow.maxSpeedScale;
            if (targetVelocity.LengthSquared() > maxSpeed * maxSpeed)
            {
                targetVelocity.Normalize();
                targetVelocity *= maxSpeed;
            }

            drone.currentVelocity = Vector3::Lerp(drone.currentVelocity, targetVelocity,
                                                  (std::min)(kSoftFollow.damping * dt, 1.0f));

            // 機首は実際の進行方向へ向ける。隊列のしなりが姿勢にも出る
            Vector3 look = drone.currentVelocity;
            if (look.LengthSquared() > 0.01f)
            {
                look.Normalize();
                TurnToward(trans, look, /*includePitch*/ true, kSoftFollow.turnRate, dt);
            }
        }
    } // namespace

    // ============================================================
    // 姿勢
    // ============================================================

    Quaternion LookToRotation(const Vector3 &look, bool includePitch)
    {
        // 前方 (0,0,-1) を pitch → yaw の順に回すと (-cos p·sin θ, sin p, -cos p·cos θ)
        // これを look と突き合わせると θ = atan2(-x, -z)、p = asin(y)
        const float yaw = atan2f(-look.x, -look.z);

        if (!includePitch)
            return Quaternion::CreateFromAxisAngle(Vector3::UnitY, yaw);

        const float pitch = asinf(std::clamp(look.y, -1.0f, 1.0f));
        return Quaternion::CreateFromYawPitchRoll(yaw, pitch, 0.0f);
    }



    void TurnToward(TransformComponent &trans, const Vector3 &look, bool includePitch, float turnRate, float dt)
    {
        trans.rotation = Quaternion::Slerp(trans.rotation, LookToRotation(look, includePitch),
                                           (std::min)(turnRate * dt, 1.0f));
    }



    // ============================================================
    // 持ち場へ戻る・持ち場を保つ
    // ============================================================

    void MoveToSlot(TransformComponent &trans, DroneComponent &drone, float dt)
    {
        ApplySpring(trans, drone, dt, MoveToTargetSpring());

        if (drone.targetLookDirection.LengthSquared() > 0.001f)
        {
            Vector3 lookDir = drone.targetLookDirection;
            lookDir.Normalize();
            TurnToward(trans, lookDir, /*includePitch*/ true, kMoveToTarget.turnRate, dt);
        }
    }



    void HoldSlot(TransformComponent &trans, DroneComponent &drone, float dt)
    {
        // 処理フロー:
        // 1. 弱いバネで持ち場へ寄る速度を決める
        // 2. ホバーの上下揺れを足す
        // 3. 予告の震えを位置に足す
        // 4. 機首を水平に向ける

        // --- バネ追従 ---

        // 陣形を切り替えた直後は hasReachedSlot が立ったままなので、Idle のまま新しい持ち場へ向かう
        // 弱いバネでは間に合わないので、遠い間は MoveToTarget の強いバネで寄せる
        const float distance = Vector3::Distance(trans.position, drone.targetPosition);
        ApplySpring(trans, drone, dt, (distance > gTuning.farFromSlot) ? MoveToTargetSpring() : kIdle);

        // --- ホバーの揺れ ---

        // 位相は機体ごとに違うので、群れが同期して揺れない
        drone.currentVelocity.y +=
            std::cos(drone.hoverTimeOffset * kHover.frequency) * kHover.amplitude * dt;

        // --- 予告の震え ---

        // 振れ幅は二乗で立ち上げ、撃つ直前に急に大きくする（線形だと直前が読めない）
        if (drone.shivers && drone.telegraphTime > 0.0f)
        {
            const float t   = (std::min)(1.0f, drone.telegraphTime / kShiver.rampTime);
            const float amp = kShiver.amplitude * t * t;

            // 速度ではなく座標を、今フレームと前フレームの差分だけ動かす
            // 速度で揺らすと振れ幅が「振幅 ÷ 角速度」に縮み、座標に足しっぱなしにすると群れごと流れていく
            trans.position = Vector3(trans.position) + ShiverOffset(drone, drone.telegraphTime, amp) - ShiverOffset(drone, drone.telegraphTime - dt, amp);
        }

        // --- 機首 ---

        if (drone.targetLookDirection.LengthSquared() > 0.001f)
            TurnToward(trans, drone.targetLookDirection, /*includePitch*/ false, kIdle.turnRate, dt);
    }



    // ============================================================
    // 突撃
    // ============================================================

    void BeginMotionFrame(CCL::ECS::World &world)
    {
        // なければ前の値（既定値）のまま
        if (world.HasResource<MotionTuning>())
            gTuning = world.GetResource<MotionTuning>();
        if (world.HasResource<SquadRegistry>())
            gSoftFollowGain = world.GetResource<SquadRegistry>().tuning.followGain;
    }



    void MoveCharge(TransformComponent &trans, DroneComponent &drone, float dt)
    {
        switch (drone.chargeMode)
        {
        case ChargeMode::RigidFollow:
            MoveRigidFollow(trans, drone, dt);
            break;
        case ChargeMode::SoftFollow:
            MoveSoftFollow(trans, drone, dt);
            break;
        default: // Straight と、知らない値
            MoveStraight(trans, drone, dt);
            break;
        }
    }



    bool IsChargeDangerous(const DroneComponent &drone)
    {
        switch (drone.chargeMode)
        {
        case ChargeMode::RigidFollow:
        case ChargeMode::SoftFollow:
            return true;
        default: // Straight と、知らない値
            return drone.fireDelay <= 0.0f && drone.chargeWindup <= 0.0f;
        }
    }



    void BeginStraightCharge(DroneComponent &drone, const ChargeBeginInfo &info)
    {
        // 処理フロー:
        // 1. 移動方式を直進に戻す
        // 2. タメの時間を入れる
        // 3. 撃つ方向がなければ決める

        // 前の技の追従モードを持ち越さないよう、直進に戻す（追従する技は陣形が毎フレーム上書きする）
        drone.chargeMode   = ChargeMode::Straight;
        drone.chargeWindup = kStraightCharge.windup;

        // 陣形が予備動作中に方向を書いていれば、それを使う
        if (drone.fireDirection.LengthSquared() >= 0.001f) return;

        // 書いていなければプレイヤーへ向ける。いなければモデルの前方（-Z）
        Vector3 dir = -Vector3::UnitZ;
        if (info.hasPlayer)
        {
            Vector3 toPlayer = info.playerPos - drone.targetPosition;
            if (toPlayer.LengthSquared() > 0.001f)
            {
                toPlayer.Normalize();
                dir = toPlayer;
            }
        }

        drone.fireDirection       = dir;
        drone.targetLookDirection = dir;
    }

} // namespace CCL::AI::Motion
