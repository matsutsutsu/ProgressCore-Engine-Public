// RootMotionSystem.cpp
#include "RootMotionSystem.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include <algorithm>

using namespace CCL::ECS;
using namespace DirectX::SimpleMath;

void RootMotionSystem::Update(float /*rawDt*/)
{
    ForEach([&](RootMotionComponent             &rm,
                CharacterMovementInputComponent &input,
                const AnimatorComponent         &animator,
                const AnimStateMachineComponent &fsm,
                const DashComponent             &dash,
                const TransformComponent        &trans,
                const TimeStateComponent        &time) {
        // ── ダッシュ優先：ダッシュ中はルートモーションを譲る ──
        if (dash.isDashing)
        {
            rm.enabled     = false;
            rm.justStarted = false;
            return;
        }

        // ── 現在ステートが applyRootMotion か判定 ──
        bool applyRoot = false;
        for (const auto &st : fsm.internalGraph.states)
        {
            if (st.stateHash == fsm.currentStateHash)
            {
                applyRoot = st.applyRootMotion;
                break;
            }
        }

        // 開始/終了エッジ検出（prevProgress 初期化）
        if (applyRoot && !rm.enabled)
        {
            rm.enabled     = true;
            rm.justStarted = true;
        }
        else if (!applyRoot && rm.enabled)
        {
            rm.enabled = false;
        }

        if (!rm.enabled || !animator.currentSequence)
            return;

        const AnimSequence *seq   = animator.currentSequence;
        const auto         &curve = seq->rootMotionCurve;
        if (curve.keys.empty() || seq->duration <= 0.0f)
            return;

        const float progress = std::clamp(animator.currentTimer / seq->duration, 0.0f, 1.0f);

        // 開始直後は基準を現在地に合わせ、飛びを防ぐ
        if (rm.justStarted)
        {
            rm.prevProgress = progress;
            rm.justStarted  = false;
            return;
        }

        // 累積距離カーブの差分 = このフレームの前進距離(m)
        const float deltaDist = curve.Evaluate(progress) - curve.Evaluate(rm.prevProgress);
        rm.prevProgress       = progress;

        // 時間が止まっている（ヒットストップ等）→ 移動も止める
        const float ldt = time.localDt;
        if (ldt <= 0.0001f)
        {
            input.desiredVelocity = Vector3::Zero;
            input.overrideSpeed   = 0.0f;
            return;
        }

        const float speed = deltaDist / ldt;

        // forward（trans.rotation から。Dash と同じく UnitZ を回す）
        Vector3 forward = Vector3::Transform(Vector3::UnitZ, trans.rotation);
        forward.y       = 0.0f;
        if (forward.LengthSquared() > 0.001f)
            forward.Normalize();
        else
            forward = Vector3::UnitZ;

        // Dash と同じ上書き機構（desiredVelocity=方向, overrideSpeed=速度）
        input.desiredVelocity = forward;
        input.overrideSpeed   = speed;
    });
}

REGISTER_LOGIC_SYSTEM(RootMotionSystem, Priority::LogicStage::L02_Update);