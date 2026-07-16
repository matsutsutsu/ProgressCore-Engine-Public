#include "RootMotionSystem.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include "Engine/Core/Math/CurveData.h"

using namespace CCL::ECS;
using namespace DirectX::SimpleMath;

void RootMotionSystem::Update(float /*rawDt*/)
{
    ForEachWithID([&](EntityID                          id,
                RootMotionComponent             &rm,
                CharacterMovementInputComponent &input,
                const AnimatorComponent         &animator,
                const AnimStateMachineComponent &fsm,
                const TransformComponent        &trans,
                const TimeStateComponent        &time,
                const ModelComponent            &modelComp) {
        // ダッシュはプレイヤー限定の任意コンポーネント。持っていれば参照する。
        bool isDashing = false;
        if (const auto *dash = _world->GetComponent<DashComponent>(id))
        {
            isDashing = dash->isDashing;
        }

        if (isDashing)
        {
            rm.enabled                = false;
            rm.lastEvaluatedSeq       = nullptr;
            input.isRootMotionActive  = false;
            input.rootMotionVelocity  = Vector3::Zero;
            return;
        }

        bool applyRoot = false;
        for (const auto &st : fsm.internalGraph.states)
        {
            if (st.stateHash == fsm.currentStateHash)
            {
                applyRoot = st.applyRootMotion;
                break;
            }
        }

        rm.enabled = applyRoot;

        Model *model = modelComp.GetModel();
        if (!rm.enabled || !animator.currentSequence || !model)
        {
            // ★このフレームはルートモーション非適用のステート。
            //   このComponent(rm)は唯一の書き手なので、ここで確実にフラグを下ろさないと
            //   前回ルートモーションを使ったステート（例:Slash）の値が
            //   JoltCharacterUpdateSystem 側に残留し、以後ずっと通常移動を上書きし続ける
            //  （＝Move等の意図した移動が「一度も動かなく見える」バグの原因）。
            rm.lastEvaluatedSeq       = nullptr;
            input.isRootMotionActive  = false;
            input.rootMotionVelocity  = Vector3::Zero;
            return;
        }

        const AnimSequence *seq       = animator.currentSequence;
        int                 animIndex = model->GetAnimationIndex(seq->targetAnimName.c_str());

        if (animIndex == -1 || seq->duration <= 0.0f) return;

        // ★ 究極の特定: 抽出対象を「実際に動いているボーン」にする
        const int rootNodeIndex = model->FindRealRootMotionNodeIndex(animIndex);

        // ★ 遷移（シーケンスの切り替わり）検知
        if (rm.lastEvaluatedSeq != seq)
        {
            rm.lastEvaluatedSeq  = seq;
            rm.prevTimer         = animator.currentTimer;
            rm.prevPreviousTimer = animator.previousTimer;
            // 遷移直後のフレームは基準点を合わせるため移動をスキップ
            return;
        }

        const float ldt = time.localDt;
        if (ldt <= 0.0001f)
        {
            input.desiredVelocity = Vector3::Zero;
            input.overrideSpeed   = 0.0f;
            return;
        }

        // ========================================================
        // ★ カーブ駆動ルートモーション（進捗→累積前進距離 m）
        //   rootMotionCurve が設定されていれば、ボーン抽出ではなく
        //   カーブの区間差分ぶんだけキャラの正面へ前進させる。
        // ========================================================
        if (animator.activeRootMotionCurve && !animator.activeRootMotionCurve->empty() && seq->duration > 0.0f)
        {
            const CurveData *rmc = animator.activeRootMotionCurve;

            const float p0 = std::clamp(rm.prevTimer / seq->duration, 0.0f, 1.0f);
            const float p1 = std::clamp(animator.currentTimer / seq->duration, 0.0f, 1.0f);
            rm.prevTimer   = animator.currentTimer;

            // ループで進捗が巻き戻った場合は「末尾まで＋先頭から」で連続させる
            float dist;
            if (p1 >= p0)
                dist = rmc->Evaluate(p1) - rmc->Evaluate(p0);
            else
                dist = (rmc->Evaluate(1.0f) - rmc->Evaluate(p0)) + (rmc->Evaluate(p1) - rmc->Evaluate(0.0f));

            Vector3 forward = Vector3::Transform(Vector3::UnitZ, trans.rotation);
            forward.y       = 0.0f;
            if (forward.LengthSquared() > 1e-6f) forward.Normalize();

            const float speed = dist / ldt; // 前進速度(m/s)。負値=後退

            input.desiredVelocity    = forward;
            input.overrideSpeed      = speed;
            input.isRootMotionActive = true;
            input.rootMotionVelocity = forward * speed;
            return; // ボーン抽出はスキップ
        }


        // ========================================================
        // 抽出ロジックの共通化 (Lambda)
        // ループによる時間の巻き戻りを自動で補正して前進距離を返す
        // ========================================================
        auto ExtractDelta = [&](int aIndex, float t0, float t1, float duration) -> Vector3 {
            if (t1 < t0)
            {
                Model::NodePose pose0, poseEnd, poseStart, pose1;
                model->ComputeAnimation(aIndex, rootNodeIndex, t0, pose0);
                model->ComputeAnimation(aIndex, rootNodeIndex, duration, poseEnd);
                model->ComputeAnimation(aIndex, rootNodeIndex, 0.0f, poseStart);
                model->ComputeAnimation(aIndex, rootNodeIndex, t1, pose1);
                return (Vector3(poseEnd.position) - Vector3(pose0.position)) +
                       (Vector3(pose1.position) - Vector3(poseStart.position));
            }
            else
            {
                Model::NodePose pose0, pose1;
                model->ComputeAnimation(aIndex, rootNodeIndex, t0, pose0);
                model->ComputeAnimation(aIndex, rootNodeIndex, t1, pose1);
                return Vector3(pose1.position) - Vector3(pose0.position);
            }
        };

        // ========================================================
        // 1. ローカル空間での差分抽出とブレンド (Blend)
        // ========================================================
        Vector3 finalLocalDelta = Vector3::Zero;

        if (animator.isBlending && animator.previousSequence)
        {
            // --- 過去モーションの抽出 ---
            int     prevAnimIndex = model->GetAnimationIndex(animator.previousSequence->targetAnimName.c_str());
            Vector3 deltaPrev     = Vector3::Zero;
            if (prevAnimIndex != -1)
            {
                float t0_prev        = rm.prevPreviousTimer;
                float t1_prev        = animator.previousTimer;
                rm.prevPreviousTimer = t1_prev;
                deltaPrev            = ExtractDelta(prevAnimIndex, t0_prev, t1_prev, animator.previousSequence->duration);
            }

            // --- 現在モーションの抽出 ---
            float t0_curr     = rm.prevTimer;
            float t1_curr     = animator.currentTimer;
            rm.prevTimer      = t1_curr;
            Vector3 deltaCurr = ExtractDelta(animIndex, t0_curr, t1_curr, animator.currentSequence->duration);

            // --- 線形補間 (Lerp) ---
            float blendRate = (animator.blendDuration > 0.0f) ? (animator.currentBlendTime / animator.blendDuration) : 1.0f;
            blendRate       = std::clamp(blendRate, 0.0f, 1.0f);

            finalLocalDelta = Vector3::Lerp(deltaPrev, deltaCurr, blendRate);
        }
        else
        {
            // ブレンド中ではない（通常再生）
            float t0     = rm.prevTimer;
            float t1     = animator.currentTimer;
            rm.prevTimer = t1;

            finalLocalDelta = ExtractDelta(animIndex, t0, t1, seq->duration);
        }

        // ========================================================
        // 2. エンティティのワールド空間への変換と適用
        // ========================================================
        Vector3 worldDelta = Vector3::Transform(finalLocalDelta, trans.rotation);
        worldDelta.y       = 0.0f; // 重力やジャンプは物理に任せる

        float   speed   = worldDelta.Length() / ldt;
        Vector3 forward = worldDelta;

        if (speed > 0.001f)
        {
            forward.Normalize();
        }
        else
        {
            forward   = Vector3::Transform(Vector3::UnitZ, trans.rotation);
            forward.y = 0.0f;
            forward.Normalize();
        }

        input.desiredVelocity = forward;
        input.overrideSpeed   = speed;

        // ダッシュ用の変数を汚染せず、専用領域に書き込む
        input.isRootMotionActive = true;
        input.rootMotionVelocity = forward * speed;
    });
}

REGISTER_LOGIC_SYSTEM(RootMotionSystem, Priority::LogicStage::L02_Update);