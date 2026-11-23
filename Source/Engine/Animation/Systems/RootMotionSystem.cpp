#include "RootMotionSystem.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include "Engine/Core/Math/CurveData.h"
#include <algorithm>

using namespace CCL::ECS;
using namespace DirectX::SimpleMath;



void RootMotionSystem::Update(float rawDt)
{
    // 処理フロー:
    // 1. 適用しないステップを除く（編集中・外部が制御中・ステートが使わない）
    // 2. クリップの切り替わりと、時間が止まっているステップを除く
    // 3. ルートモーションカーブがあれば、カーブの差分で前進させる
    // 4. なければ、ルートボーンの移動量を水平な速度にして書く

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
        // 1. 適用しないステップ
        // ---------------------------------------------------------
        Model *model = modelComp.GetModel();
        if (!ShouldApply(rm, movement, animator, fsm, model)) return;

        const AnimSequence *seq       = animator.currentSequence;
        const int           animIndex = model->GetAnimationIndex(seq->targetAnimName.c_str());
        if (animIndex == -1 || seq->duration <= 0.0f)
        {
            ClearOutput(movement);
            return;
        }

        // ---------------------------------------------------------
        // 2. 切り替わりと停止中
        // ---------------------------------------------------------

        // 切り替わった直後のステップは基準の時刻を合わせるだけで、移動しない
        if (ResetOnClipChange(rm, animator))
        {
            ClearOutput(movement);
            return;
        }

        // ヒットストップ・ポーズ中は、速度を求められない（0 除算）ので出力を消す
        //   フラグだけ立てたまま抜けると、読み手が古い rootMotionVelocity を今の速度として使い、
        //   バンク角やダッシュ攻撃の判定に影響する
        const float ldt = time.localDt;
        if (ldt <= 0.0001f)
        {
            ClearOutput(movement);
            movement.desiredVelocity = Vector3::Zero;
            return;
        }

        // ---------------------------------------------------------
        // 3. ルートモーションカーブ
        // ---------------------------------------------------------

        // カーブがあれば、ボーンではなくカーブの差分だけ正面へ前進させる
        if (const CurveData *curve = animator.GetRootMotionCurve())
        {
            const float dist = AdvanceCurveDistance(rm, animator, *curve);
            WriteOutput(movement, FlatForward(trans), dist / ldt);
            return;
        }

        // ---------------------------------------------------------
        // 4. ルートボーンの移動量
        // ---------------------------------------------------------

        // ワールド空間の水平な移動にする。縦方向（重力・ジャンプ）は物理に任せる
        Vector3 worldDelta = Vector3::Transform(AdvanceBoneDelta(rm, animator, model, animIndex), trans.rotation);
        worldDelta.y       = 0.0f;

        const float speed = worldDelta.Length() / ldt;

        // ほとんど動いていないなら、向きはキャラの正面にする（ゼロベクトルは正規化できない）
        Vector3 forward = FlatForward(trans);
        if (speed > 0.001f)
        {
            forward = worldDelta;
            forward.Normalize();
        }
        WriteOutput(movement, forward, speed);
    });
}



bool RootMotionSystem::ShouldApply(RootMotionComponent &rm, CharacterMovementComponent &movement,
                                   const AnimatorComponent &animator, const AnimStateMachineComponent &fsm,
                                   const Model *model)
{
    // エディタ（シーケンサー）が Animator を操作している間は適用しない
    //   編集中は currentTimer が自由に飛ぶので、その差分を前進量として読むと、
    //   ルートモーションのないアニメを見ているだけでキャラが歩き出す
    bool apply = !animator.isEditorOverride;

    // 直前の値が残ると、通常移動の目標速度として使われて歩き続けるので 0 にする
    if (animator.isEditorOverride) movement.desiredVelocity = Vector3::Zero;

    // 外部（ダッシュ等）が移動を制御中、またはムービーがクリップを握っている間は適用しない
    //   ムービー中はステートが握る前のまま残るので、そのステートの applyRootMotion で判定すると、
    //   ムービーのクリップの移動量でキャラが歩き出しうる
    if (rm.externallyDriven || animator.isExternalOverride) apply = false;

    // 再生中の葉ステートはサブマシンの中にもありうるので、再帰的に探す
    if (apply)
    {
        const AnimState *st = fsm.GetCurrentState();
        apply               = st && st->applyRootMotion;
    }
    rm.enabled = apply;

    if (apply && animator.currentSequence && model) return true;

    // 次に使うステップで基準の時刻を取り直す
    //   出力を消さないと、前のステート（例: Atk_Combo1）の値が残り、通常の移動を上書きし続ける
    rm.lastEvaluatedSeq = nullptr;
    ClearOutput(movement);
    return false;
}



bool RootMotionSystem::ResetOnClipChange(RootMotionComponent &rm, const AnimatorComponent &animator)
{
    // 頭から再生し直したことは再生回数で見る
    //   ポインタだけで見ると、再生し直しを「ループで先頭へ戻った」と取り違え、
    //   前の再生の残り（末尾 - 前回の時刻）まで 1 ステップで前進してしまう
    const bool sameClip = (rm.lastEvaluatedSeq == animator.currentSequence) && (rm.lastPlayCount == animator.playCount);
    if (sameClip) return false;

    rm.lastEvaluatedSeq  = animator.currentSequence;
    rm.lastPlayCount     = animator.playCount;
    rm.prevTimer         = animator.currentTimer;
    rm.prevPreviousTimer = animator.previousTimer;
    return true;
}



float RootMotionSystem::AdvanceCurveDistance(RootMotionComponent &rm, const AnimatorComponent &animator,
                                             const CurveData &curve)
{
    const float duration = animator.currentSequence->duration;

    // カーブは進捗（0〜1）から累積の前進距離（m）を返す
    const float p0 = std::clamp(rm.prevTimer / duration, 0.0f, 1.0f);
    const float p1 = std::clamp(animator.currentTimer / duration, 0.0f, 1.0f);
    rm.prevTimer   = animator.currentTimer;

    // ループで進捗が戻った場合は「末尾まで + 先頭から」でつなぐ
    if (p1 >= p0) return curve.Evaluate(p1) - curve.Evaluate(p0);
    return (curve.Evaluate(1.0f) - curve.Evaluate(p0)) + (curve.Evaluate(p1) - curve.Evaluate(0.0f));
}



Vector3 RootMotionSystem::AdvanceBoneDelta(RootMotionComponent &rm, const AnimatorComponent &animator, Model *model,
                                           int animIndex)
{
    // 名前で決め打ちせず、実際に移動量を持つノードから抽出する
    const int rootNodeIndex = model->FindRealRootMotionNodeIndex(animIndex);

    // --- 今のクリップ ---

    // 前回の時刻は Animator の previousFrameTimer でなく、自前で控えた値を使う
    //   ヒットストップなどで評価を飛ばしたステップの分も、次に評価するステップでまとめて拾える
    const float t0 = rm.prevTimer;
    rm.prevTimer   = animator.currentTimer;

    const Vector3 deltaCurr = ExtractBoneDelta(model, animIndex, rootNodeIndex, t0, animator.currentTimer,
                                               animator.currentSequence->duration);
    if (!animator.isBlending || !animator.previousSequence) return deltaCurr;

    // --- 前のクリップ（ブレンド中だけ）---
    const AnimSequence *prevSeq   = animator.previousSequence;
    const int           prevIndex = model->GetAnimationIndex(prevSeq->targetAnimName.c_str());

    Vector3 deltaPrev = Vector3::Zero;
    if (prevIndex != -1)
    {
        const float prevT0   = rm.prevPreviousTimer;
        rm.prevPreviousTimer = animator.previousTimer;

        // ルートボーンの番号はクリップごとに引く（AnimationPoseSystem と同じ）
        //   今のクリップの番号を使い回すと、ルートの違うクリップどうしのブレンドで別のボーンの移動量を読む
        const int prevRootIndex = model->FindRealRootMotionNodeIndex(prevIndex);
        deltaPrev = ExtractBoneDelta(model, prevIndex, prevRootIndex, prevT0, animator.previousTimer, prevSeq->duration);
    }

    // 前後のクリップの移動量を、ポーズと同じブレンド率で補間する
    return Vector3::Lerp(deltaPrev, deltaCurr, animator.GetBlendRate());
}



Vector3 RootMotionSystem::ExtractBoneDelta(Model *model, int animIndex, int rootNodeIndex, float t0, float t1,
                                           float duration)
{
    if (t1 >= t0)
    {
        Model::NodePose pose0, pose1;
        model->ComputeAnimation(animIndex, rootNodeIndex, t0, pose0);
        model->ComputeAnimation(animIndex, rootNodeIndex, t1, pose1);
        return Vector3(pose1.position) - Vector3(pose0.position);
    }

    Model::NodePose pose0, poseEnd, poseStart, pose1;
    model->ComputeAnimation(animIndex, rootNodeIndex, t0, pose0);
    model->ComputeAnimation(animIndex, rootNodeIndex, duration, poseEnd);
    model->ComputeAnimation(animIndex, rootNodeIndex, 0.0f, poseStart);
    model->ComputeAnimation(animIndex, rootNodeIndex, t1, pose1);
    return (Vector3(poseEnd.position) - Vector3(pose0.position)) + (Vector3(pose1.position) - Vector3(poseStart.position));
}



Vector3 RootMotionSystem::FlatForward(const TransformComponent &trans)
{
    Vector3 forward = Vector3::Transform(Vector3::UnitZ, trans.rotation);
    forward.y       = 0.0f;
    if (forward.LengthSquared() > 1e-6f) forward.Normalize();
    return forward;
}



void RootMotionSystem::WriteOutput(CharacterMovementComponent &movement, const Vector3 &forward, float speed)
{
    // overrideSpeed は書かない
    //   isRootMotionActive を立てると、読み手は rootMotionVelocity を使って先に return するので使われない
    //   desiredVelocity は PlayerAnimParamSyncSystem が読むので書く
    movement.desiredVelocity    = forward;
    movement.isRootMotionActive = true;
    movement.rootMotionVelocity = forward * speed;
}



void RootMotionSystem::ClearOutput(CharacterMovementComponent &movement)
{
    // overrideSpeed は書かない（読み手が既定値に戻す）
    movement.isRootMotionActive = false;
    movement.rootMotionVelocity = Vector3::Zero;
}



REGISTER_LOGIC_SYSTEM(RootMotionSystem, Priority::LogicStage::L02_Update);
