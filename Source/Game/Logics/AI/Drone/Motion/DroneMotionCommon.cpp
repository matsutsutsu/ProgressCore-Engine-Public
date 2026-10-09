#include "DroneMotionCommon.h"

#include <algorithm>
#include <cmath>

using namespace DirectX::SimpleMath;

namespace CCL::AI::Motion
{
    namespace
    {
        // ============================================================
        // 調整値
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

        // ============================================================
        // 内部の部品
        // ============================================================

        /**
         * @brief バネ追従で drone.currentVelocity を決める（Idle と MoveToTarget）。
         *
         * 持ち場とのずれに比例した速度を目標にし、今の速度をそこへ少しずつ寄せる。
         *
         * @param trans   機体の Transform（今の位置を読む）。
         * @param drone  動かす機体。targetPosition・followFeedForward・springScale を読み、currentVelocity を書く。
         * @param dt     この機体の経過時間 [秒]。
         * @param tuning バネの強さ。陣形が書いた drone.springScale を掛けて使う。
         */
        void ApplySpring(const TransformComponent &trans, DroneComponent &drone, float dt,
                         const SpringTuning &tuning)
        {
            Vector3 displacement   = drone.targetPosition - trans.position;
            Vector3 targetVelocity = displacement * (tuning.gain * drone.springScale);

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

        /**
         * @brief 予告の震えの位置オフセットを返す。正弦波1本だけで作る。
         *
         * t を取るのは、前フレームぶんを引いて差分だけ動かすため。
         * 足しっぱなしにすると座標へ溜まって群れごと流れていく。
         *
         * 3軸同時には揺らさない。
         * 軸ごとに周波数をずらさない限り小さな楕円をなぞるだけで、ずらすには波を重ねる必要があり、
         * 三角関数が 1000機 × 12回／フレームに膨れる。
         *
         * 揺らす軸は隣同士で変える（localIndex % 3）。
         * 1本の波でも、群れの表面だけがざわついて見える。
         *
         * @param drone     震わせる機体。位相と震える軸を localIndex から決める。
         * @param t         構え始めてからの経過時間（秒）。
         * @param amplitude 震えの振幅（m）。
         * @param shiver    震えの調整値。
         * @return 位置オフセット（m）。
         */
        Vector3 ShiverOffset(const DroneComponent &drone, float t, float amplitude, const DroneShiverTuning &shiver)
        {
            // 位相を機体ごとにずらす。揃っていると全機が同時に膨らんで「呼吸」に見える。
            const float phase = t * shiver.frequency * DirectX::XM_2PI + drone.localIndex * kShiverPhaseStep * shiver.scatter;

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
    } // namespace

    // ============================================================
    // 姿勢
    // ============================================================

    Quaternion LookToRotation(const Vector3 &look, bool includePitch)
    {
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

    void MoveToSlot(TransformComponent &trans, DroneComponent &drone, float dt, const MotionContext &ctx)
    {
        ApplySpring(trans, drone, dt, kMoveToTarget);

        if (drone.targetLookDirection.LengthSquared() > 0.001f)
        {
            Vector3 lookDir = drone.targetLookDirection;
            lookDir.Normalize();
            TurnToward(trans, lookDir, /*includePitch*/ true, kMoveToTarget.turnRate, dt);
        }
    }

    void HoldSlot(TransformComponent &trans, DroneComponent &drone, float dt, const MotionContext &ctx)
    {
        // 処理フロー:
        // 1. 弱いバネで持ち場へ寄る速度を決める
        // 2. ホバーの上下揺れを足す
        // 3. 予告の震えを位置に足す
        // 4. 機首を水平に向ける

        // --- バネ追従 ---

        ApplySpring(trans, drone, dt, kIdle);

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
            const float t   = (std::min)(1.0f, drone.windupTimer / ctx.shiver.rampTime);
            const float amp = ctx.shiver.amplitude * t * t;

            // 速度ではなく座標を、差分（今フレーム − 前フレーム）で動かす。
            // 速度に足すと、振れ幅が「振幅 ÷ 角速度」まで縮んで指定値と見た目が一致しない。
            // 座標に足しっぱなしにすると、座標へ溜まって群れごと流れていく。
            trans.position = Vector3(trans.position) + ShiverOffset(drone, drone.windupTimer, amp, ctx.shiver) - ShiverOffset(drone, drone.windupTimer - dt, amp, ctx.shiver);
        }

        // --- 機首 ---

        if (drone.targetLookDirection.LengthSquared() > 0.001f)
            TurnToward(trans, drone.targetLookDirection, /*includePitch*/ false, kIdle.turnRate, dt);
    }

} // namespace CCL::AI::Motion
