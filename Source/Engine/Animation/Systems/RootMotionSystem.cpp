#include "RootMotionSystem.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include "Engine/Core/Math/CurveData.h"

using namespace CCL::ECS;
using namespace DirectX::SimpleMath;

namespace
{
    // ルートモーションの出力を無効にする。overrideSpeed は書かない（読み手が既定値に戻す）
    //   途中で抜ける経路が多いので、消すものをここに集め、どの経路からも同じ状態で抜ける
    void ClearRootMotionOutput(CharacterMovementComponent &movement)
    {
        movement.isRootMotionActive = false;
        movement.rootMotionVelocity = Vector3::Zero;
    }
} // namespace



void RootMotionSystem::Update(float /*rawDt*/)
{
    // 処理フロー:
    // 1. 適用しない場合を除く（編集中・外部が制御中・ステートが使わない）
    // 2. クリップの切り替わりと、時間が止まっているステップを除く
    // 3. ルートモーションカーブがあれば、カーブの差分で前進させる
    // 4. なければ、ルートボーンの移動量から前進量を求める（ブレンド中は前後を補間）
    // 5. ワールド空間の速度にして書く

    // 出力は CharacterMovementComponent.rootMotionVelocity。動かすのは JoltCharacterUpdateSystem
    // 適用しない経路では、必ず出力を消してから抜ける（前のステートの値が残ると移動を上書きし続ける）

    ForEach([&](RootMotionComponent             &rm,
                CharacterMovementComponent      &movement,
                const AnimatorComponent         &animator,
                const AnimStateMachineComponent &fsm,
                const TransformComponent        &trans,
                const TimeStateComponent        &time,
                const ModelComponent            &modelComp) {
        // ---------------------------------------------------------
        // 1. 適用しない場合
        // ---------------------------------------------------------

        // エディタ（シーケンサー）が Animator を操作している間は適用しない
        //   編集中は currentTimer が自由に飛ぶので、その差分を前進量として読むと、
        //   ルートモーションのないアニメを見ているだけでキャラが歩き出す
        if (animator.isEditorOverride)
        {
            rm.enabled          = false;
            rm.lastEvaluatedSeq = nullptr; // 編集終了後の最初のステップで基準時刻を取り直す

            // 直前の値が残ると、通常移動の目標速度として使われて歩き続けるので 0 にする
            ClearRootMotionOutput(movement);
            movement.desiredVelocity = Vector3::Zero;
            return;
        }

        // 外部（ダッシュ等）が移動を制御中、またはムービーがクリップを握っている間は適用しない
        //   ムービー中はステートが握る前のまま残るので、そのステートの applyRootMotion で判定すると、
        //   ムービーのクリップの移動量でキャラが歩き出しうる
        if (rm.externallyDriven || animator.isExternalOverride)
        {
            rm.enabled          = false;
            rm.lastEvaluatedSeq = nullptr;
            ClearRootMotionOutput(movement);
            return;
        }

        // 再生中の葉ステートはサブマシンの中にもありうるので、再帰的に探す
        bool applyRoot = false;
        if (const AnimState *st = FindStateRecursive(fsm.internalGraph, fsm.currentStateHash))
            applyRoot = st->applyRootMotion;

        rm.enabled = applyRoot;

        Model *model = modelComp.GetModel();
        if (!rm.enabled || !animator.currentSequence || !model)
        {
            // ルートモーションを使わないステート
            //   出力を消さないと、前のステート（例: Attack_1）の値が残り、通常の移動を上書きし続ける
            rm.lastEvaluatedSeq = nullptr;
            ClearRootMotionOutput(movement);
            return;
        }

        const AnimSequence *seq       = animator.currentSequence;
        int                 animIndex = model->GetAnimationIndex(seq->targetAnimName.c_str());

        if (animIndex == -1 || seq->duration <= 0.0f)
        {
            ClearRootMotionOutput(movement);
            return;
        }

        // 名前で決め打ちせず、実際に移動量を持つノードから抽出する
        const int rootNodeIndex = model->FindRealRootMotionNodeIndex(animIndex);

        // ---------------------------------------------------------
        // 2. 切り替わりと停止中
        // ---------------------------------------------------------

        // クリップが切り替わった、または同じクリップを頭から再生し直した
        //   ポインタだけで見ると、再生し直しを「ループで先頭へ戻った」と取り違え、
        //   前の再生の残り（末尾 - 前回の時刻）まで 1 ステップで前進してしまう
        if (rm.lastEvaluatedSeq != seq || rm.lastPlayCount != animator.playCount)
        {
            rm.lastEvaluatedSeq  = seq;
            rm.lastPlayCount     = animator.playCount;
            rm.prevTimer         = animator.currentTimer;
            rm.prevPreviousTimer = animator.previousTimer;

            // 切り替わった直後のステップは基準の時刻を合わせるだけで、移動しない
            ClearRootMotionOutput(movement);
            return;
        }

        const float ldt = time.localDt;
        if (ldt <= 0.0001f)
        {
            // ヒットストップ・ポーズ中。速度を求められない（0 除算）ので出力を消す
            //   フラグだけ立てたまま抜けると、読み手が古い rootMotionVelocity を今の速度として使い、
            //   バンク角やダッシュ攻撃の判定に影響する
            ClearRootMotionOutput(movement);
            movement.desiredVelocity = Vector3::Zero;
            return;
        }

        // ---------------------------------------------------------
        // 3. ルートモーションカーブ
        // ---------------------------------------------------------

        // カーブ（進捗 → 累積前進距離 m）があれば、ボーンではなくカーブの差分だけ正面へ前進させる
        if (animator.activeRootMotionCurve && !animator.activeRootMotionCurve->empty() && seq->duration > 0.0f)
        {
            const CurveData *rmc = animator.activeRootMotionCurve;

            const float p0 = std::clamp(rm.prevTimer / seq->duration, 0.0f, 1.0f);
            const float p1 = std::clamp(animator.currentTimer / seq->duration, 0.0f, 1.0f);
            rm.prevTimer   = animator.currentTimer;

            // ループで進捗が戻った場合は「末尾まで + 先頭から」でつなぐ
            float dist;
            if (p1 >= p0)
                dist = rmc->Evaluate(p1) - rmc->Evaluate(p0);
            else
                dist = (rmc->Evaluate(1.0f) - rmc->Evaluate(p0)) + (rmc->Evaluate(p1) - rmc->Evaluate(0.0f));

            Vector3 forward = Vector3::Transform(Vector3::UnitZ, trans.rotation);
            forward.y       = 0.0f;
            if (forward.LengthSquared() > 1e-6f) forward.Normalize();

            const float speed = dist / ldt; // 前進速度（m/s）。負なら後退

            // overrideSpeed は書かない
            //   isRootMotionActive を立てると、読み手は rootMotionVelocity を使って先に return するので使われない
            //   desiredVelocity は PlayerAnimParamSyncSystem が読むので書く
            movement.desiredVelocity    = forward;
            movement.isRootMotionActive = true;
            movement.rootMotionVelocity = forward * speed; // 後退も負の speed で表す
            return;
        }

        // ---------------------------------------------------------
        // 4. ルートボーンの移動量
        // ---------------------------------------------------------

        // t0 → t1 の移動量を求める。ループで時刻が戻った場合は「末尾まで + 先頭から」でつなぐ
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

        Vector3 finalLocalDelta = Vector3::Zero; // ローカル空間での移動量

        if (animator.isBlending && animator.previousSequence)
        {
            // ブレンド中は前後のクリップの移動量を、ポーズと同じブレンド率で補間する

            // --- 前のクリップ ---
            int     prevAnimIndex = model->GetAnimationIndex(animator.previousSequence->targetAnimName.c_str());
            Vector3 deltaPrev     = Vector3::Zero;
            if (prevAnimIndex != -1)
            {
                float t0_prev        = rm.prevPreviousTimer;
                float t1_prev        = animator.previousTimer;
                rm.prevPreviousTimer = t1_prev;
                deltaPrev            = ExtractDelta(prevAnimIndex, t0_prev, t1_prev, animator.previousSequence->duration);
            }

            // --- 現在のクリップ ---
            float t0_curr     = rm.prevTimer;
            float t1_curr     = animator.currentTimer;
            rm.prevTimer      = t1_curr;
            Vector3 deltaCurr = ExtractDelta(animIndex, t0_curr, t1_curr, animator.currentSequence->duration);

            float blendRate = (animator.blendDuration > 0.0f) ? (animator.currentBlendTime / animator.blendDuration) : 1.0f;
            blendRate       = std::clamp(blendRate, 0.0f, 1.0f);

            finalLocalDelta = Vector3::Lerp(deltaPrev, deltaCurr, blendRate);
        }
        else
        {
            float t0     = rm.prevTimer;
            float t1     = animator.currentTimer;
            rm.prevTimer = t1;

            finalLocalDelta = ExtractDelta(animIndex, t0, t1, seq->duration);
        }

        // ---------------------------------------------------------
        // 5. ワールド空間の速度にする
        // ---------------------------------------------------------
        Vector3 worldDelta = Vector3::Transform(finalLocalDelta, trans.rotation);
        worldDelta.y       = 0.0f; // 縦方向（重力・ジャンプ）は物理に任せる

        float   speed   = worldDelta.Length() / ldt;
        Vector3 forward = worldDelta;

        // ほとんど動いていないなら、向きはキャラの正面にする（ゼロベクトルは正規化できない）
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

        // overrideSpeed は書かない（カーブの場合と同じ理由）
        movement.desiredVelocity = forward;
        movement.isRootMotionActive = true;
        movement.rootMotionVelocity = forward * speed;
    });
}



REGISTER_LOGIC_SYSTEM(RootMotionSystem, Priority::LogicStage::L02_Update);
