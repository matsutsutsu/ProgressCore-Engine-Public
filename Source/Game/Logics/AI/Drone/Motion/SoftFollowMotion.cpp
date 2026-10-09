#include "SoftFollowMotion.h"

#include <algorithm>

#include "Game/Logics/AI/Drone/Motion/DroneMotionCommon.h"
#include "Game/Logics/AI/Drone/Squad/SquadState.h"
#include "Engine/ECS/Core/CCL_World.h"

using namespace DirectX::SimpleMath;

namespace CCL::AI::Motion
{
    namespace
    {
        // バネ追従の調整値。バネの強さは SquadTuning から実行中に写し取る（BeginFrame）。
        struct SoftFollowTuning
        {
            float damping;       // 今の速度を目標の速度へ寄せる速さ [1/s]
            float maxSpeedScale; // 出せる速さの上限（moveSpeed 倍）
            float turnRate;      // 機首を向け直す速さ [1/s]
        };

        // 上限は、小隊のアンカーの突入速度を上回る必要がある（低いと隊列から離れて置いていかれる）。
        constexpr SoftFollowTuning kSoftFollow{.damping = 10.0f, .maxSpeedScale = 2.4f, .turnRate = 12.0f};
    } // namespace

    void SoftFollowMotion::BeginFrame(CCL::ECS::World &world)
    {
        // 小隊の調整 UI で変えたバネの強さを写し取る。無ければ前の値（既定値）のまま。
        if (world.HasResource<SquadRegistry>())
            m_followGain = world.GetResource<SquadRegistry>().tuning.followGain;
    }

    void SoftFollowMotion::Move(TransformComponent &trans, DroneComponent &drone, float dt,
                                const MotionContext &ctx) const
    {
        const float pGain = m_followGain * drone.springScale; // 後方の機ほど弱める＝しなる（springScale は小隊の陣形が書く）

        // 先渡しの速度 ＋ 目標からのずれの補正（バネ）。先渡しがあるぶんバネは小さくてよい。
        //
        // Idle / MoveToTarget（DroneMotionCommon の ApplySpring）と違い、上限は先渡しの速度ごと掛ける。
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

} // namespace CCL::AI::Motion
