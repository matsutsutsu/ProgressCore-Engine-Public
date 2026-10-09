#include "RigidFollowMotion.h"

#include "Game/Logics/AI/Drone/Motion/DroneMotionCommon.h"

using namespace DirectX::SimpleMath;

namespace CCL::AI::Motion
{
    namespace
    {
        // 剛体追従の調整値。
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
            .turnRate        = 25.0f, // MoveToTarget と同じ速さで向け直す（DroneMotionCommon.cpp）
        };
    } // namespace

    void RigidFollowMotion::Move(TransformComponent &trans, DroneComponent &drone, float dt,
                                 const MotionContext &ctx) const
    {
        // 追従モードは陣形自体が動く攻撃（縦割りのヒンジ回転など）。
        // 直進せず、Formation が毎フレーム動かす targetPosition を追う。タメ（stateTimer）は使わない。

        // --- 動き切った後 ---

        // 形が動き切ったら剛体をやめ、Idle のバネへ渡して惰性で収める。
        // 閉じ切った座標に固定し続けると、全機が同時に静止して不自然に見えるため。

        // 手を離すのは動きだけで、当たり方は変わらない。
        // 危険な区間かどうかは currentState が持っている（DroneHitSystem）。
        if (drone.attackProgress >= 1.0f)
        {
            HoldSlot(trans, drone, dt, ctx);
            return;
        }

        // --- 剛体 ---

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
    }

} // namespace CCL::AI::Motion
