#include "AnimationSystem.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include <cmath>
#include <algorithm>
#include <tracy/Tracy.hpp>

#include "Engine/Animation/Core/AnimEventMessages.h"
#include "Engine/Animation/Core/AnimPoseUtils.h"
#include "Engine/Core/Math/CurveData.h"

#include "Engine/Graphics/Components/Core/ModelComponent.h"
#include "Engine/Graphics/Resource/ModelResource.h"
#include "Engine/Animation/Components/AnimatorComponent.h"
#include "Engine/Animation/Components/RootMotionComponent.h"
#include "Engine/Time/TimeState.h"

// ===================================================================================
// ファイル: AnimationSystem.cpp
// 概要: アニメーションの「時間進行」と「イベント発火」を担う実働システム
//
// [ イベント発火の方針 ]
//   点イベント(Sound/Effect/SpawnHitbox) は EventBus で1回だけ発火。
//   区間状態イベント(HitBox/Trail/Blackboard) は発火せず、各専用システムが
//   区間ポーリングで読み取る（AnimEventType で型安全に分岐）。
// ===================================================================================

namespace
{
// -----------------------------------------------------------------------------------
// [責務] m_world に依存しない純粋な計算群。
//        単体テストしやすく、ヘッダにも公開しない（実装詳細のため匿名namespaceに隔離）。
// -----------------------------------------------------------------------------------

// TODO: 実行中にこの部分の処理負荷がとてつもないフレームがあったから後で調べる まれに出てくる
// 速度カーブによる時間の歪み(Time Warping)を考慮した実効再生速度を返す。
float ComputeEffectiveSpeed(const AnimatorComponent &animator, const AnimSequence *seq)
{
    ZoneScopedN("Anim::ComputeEffectiveSpeed");

    float curveMultiplier = 1.0f;
    if (animator.activeCurve)
    {
        float progress  = (seq->duration > 0.0f) ? (animator.currentTimer / seq->duration) : 0.0f;
        // ループ境界を跨いだフレームでは currentTimer が duration を超え progress>1 になり、
        //   速度カーブが外挿されて速度が一瞬跳ねる。進捗を [0,1] に丸めて外挿スパイクを防ぐ。
        progress        = std::clamp(progress, 0.0f, 1.0f);
        curveMultiplier = animator.activeCurve->Evaluate(progress);
    }
    return animator.playbackSpeed * curveMultiplier;
}

/**
 * @brief currentTimer（と、ブレンド中なら previousTimer）を dt ぶん進める。
 * @return 順方向に進んだか（逆再生・シーク検出用。イベント発火可否の判定に使う）。
 */
bool AdvanceTimers(AnimatorComponent &animator, const AnimSequence *seq, float localDt)
{
    ZoneScopedN("Anim::AdvanceTimers");

    const float effectiveSpeed = ComputeEffectiveSpeed(animator, seq);
    const float prevTime       = animator.currentTimer;

    // このフレームで進める前の時刻を記録（区間HitBoxのスイープ判定に使う）。
    animator.previousFrameTimer = prevTime;

    animator.currentTimer += localDt * effectiveSpeed;
    const bool movedForward = (animator.currentTimer >= prevTime);

    // 過去の記憶（ブレンド）のタイマーを進行させる
    if (animator.isBlending)
    {
        animator.currentBlendTime += localDt;

        // 過去のモーションは「過去のスピード」で進める
        animator.previousTimer += localDt * animator.previousPlaybackSpeed;

        if (animator.previousSequence && animator.previousSequence->duration > 0.0f)
        {
            if (!animator.previousLoop)
            {
                // 非ループ (JumpEndなど): durationを超えたら末尾でフリーズさせる
                animator.previousTimer = (std::min)(animator.previousTimer, animator.previousSequence->duration);
            }
            else
            {
                // ループ (Runなど): 安全に剰余計算してループさせる
                animator.previousTimer = std::fmod(animator.previousTimer, animator.previousSequence->duration);
                if (animator.previousTimer < 0.0f)
                    animator.previousTimer += animator.previousSequence->duration;
            }
        }

        if (animator.currentBlendTime >= animator.blendDuration)
        {
            animator.isBlending       = false;
            animator.previousSequence = nullptr;
        }
    }

    return movedForward;
}

// duration境界でのループ/終了判定。currentTimer を正規化する。
void ResolveLoopAndFinish(AnimatorComponent &animator, const AnimSequence *seq)
{
    ZoneScopedN("Anim::ResolveLoopAndFinish");

    if (seq->duration <= 0.0f) return;

    if (animator.currentTimer >= seq->duration)
    {
        if (animator.isLoop)
        {
            animator.currentTimer   = std::fmod(animator.currentTimer, seq->duration);
            animator.nextEventIndex = 0;
        }
        else
        {
            animator.currentTimer = seq->duration;
            animator.isFinished   = true;
        }
    }
    else if (animator.currentTimer < 0.0f)
    {
        if (animator.isLoop)
        {
            animator.currentTimer = std::fmod(animator.currentTimer, seq->duration) + seq->duration;
            animator.SyncEventCursor();
        }
        else
        {
            animator.currentTimer = 0.0f;
        }
    }
}

} // namespace

using CCL::Anim::PinRootMotionNode;

AnimationSystem::AnimationSystem()
    : IfSystem("AnimationSystem")
{
}

void AnimationSystem::Update(float dt)
{
    // 直列実行。DispatchPointEvents が EventBus へ Publish するため、
    // 並列化するならまず EventBus 側のスレッド安全性を確保すること。
    //
    // ModelComponent はクエリの絞り込み（Model を持つキャラだけを対象にする）目的でのみ
    // 受け取っており、ここでは触らない。ポーズ計算は AnimationPoseSystem が行う。
    ForEachWithID([this](CCL::ECS::EntityID id, AnimatorComponent &animator, ModelComponent &modelComp, const TimeStateComponent &time) {

        // 台本（Sequence）がセットされていなければ何もしない
        if (!animator.currentSequence) return;

        // エディタで操作中の場合は、通常の時間進行とイベント発火をストップする
        if (animator.isEditorOverride) return;

        const AnimSequence *seq = animator.currentSequence;

        // 1. 時間の進行（カーブによる時間の歪みを含む）
        const bool movedForward = AdvanceTimers(animator, seq, time.localDt);

        // 2. イベント発火 (O(1)栞アクセス)
        DispatchPointEvents(id, animator, seq, movedForward);

        // 3. ループと終了の判定
        ResolveLoopAndFinish(animator, seq);

        // 4. 骨格ポーズの計算はここでは行わない。
        //    AnimationPoseSystem（Render / R02b_SkeletalPose）が1描画フレームに1回だけ実行する。
        //    本システムは FixedUpdate 側なので最大4回まわるが、ポーズを行列へ変換するのは
        //    描画1回きり＝ここで計算すると最後のステップ以外が丸ごと捨てられるため。
        (void)modelComp;
    });
}

void AnimationSystem::DispatchPointEvents(CCL::ECS::EntityID id, AnimatorComponent &animator,
    const AnimSequence *seq, bool movedForward)
{
    ZoneScopedN("AnimationSystem::DispatchPointEvents");

    if (!movedForward)
    {
        // 逆再生・シーク: 発火せず栞だけ現在時刻に合わせる
        animator.SyncEventCursor();
        return;
    }

    while (animator.nextEventIndex < static_cast<int>(seq->events.size()))
    {
        const auto &evt = seq->events[animator.nextEventIndex];

        // まだイベントの時間に達していなければ走査終了
        if (animator.currentTimer < evt.startTime)
            break;

        switch (evt.type)
        {
        case AnimEventType::Sound:
            // 点イベント: FMOD 再生を依頼
            m_world->GetEventBus().Publish(AnimEventSoundMessage{id, evt.stringParam});
            break;

        case AnimEventType::Effect:
            // 点イベント: Effekseer 生成を依頼（オフセット/スケール/追従を渡す）
            m_world->GetEventBus().Publish(
                AnimEventEffectMessage{id, evt.stringParam, evt.offset, evt.effectScale, evt.effectFollow});
            break;

        case AnimEventType::SpawnHitbox:
        {
            // 点イベント: 空間に独立した球状ヒットボックスを生成依頼。
            //   球の生存時間は Sequencer のブロック長(区間)をそのまま渡す。
            //   点(startTime==endTime)なら 0 になり、購読側が最小寿命へフォールバックする。
            const float window = evt.endTime - evt.startTime;
            m_world->GetEventBus().Publish(AnimEventSpawnHitboxMessage{
                id, evt.offset, evt.radius, evt.damage, evt.hitStopDuration, evt.hitStopScale, window,
                evt.attackLayer, evt.knockback});
            break;
        }

        case AnimEventType::HitBox:
        case AnimEventType::Trail:
        case AnimEventType::Particle:
        case AnimEventType::Blackboard:
            // 区間状態イベント: 専用システムが区間ポーリングで読むため発火しない。
            //   HitBox      → CombatAnimationSyncSystem
            //   Trail       → TrailAnimationSyncSystem
            //   Particle    → ParticleAnimationSyncSystem
            //   Blackboard  → (AIのBB更新システム / 現状未実装)
            break;

        default:
            break;
        }

        animator.nextEventIndex++;
    }
}

// エディタ（AnimationSequencerWindow）から呼ばれる手動更新処理
void AnimationSystem::UpdateManual(CCL::ECS::EntityID entity, float time)
{
    ZoneScopedN("AnimationSystem::UpdateManual");

    auto *animator  = m_world->GetComponent<AnimatorComponent>(entity);
    auto *modelComp = m_world->GetComponent<ModelComponent>(entity);

    if (!animator || !modelComp || !modelComp->GetModel())
        return;

    Model *model = modelComp->GetModel();

    const AnimSequence *seq = animator->currentSequence;
    if (!seq)
        return;

    int animIndex = model->GetAnimationIndex(seq->targetAnimName.c_str());
    if (animIndex == -1)
        return;

    model->ComputeAnimation(animIndex, time, animator->poseBufferA);

    // ========================================================
    // Force Root Lock（エディタプレビュー版）
    //   実行時の Update() と同様に、ルートボーンのXZ移動をクリップの初期姿勢へ
    //   ピン留めする。これが無いと、クリップに焼き込まれたルートボーン自身の
    //   前進(スケルトンのアニメーション)と、RootMotionカーブ由来の Transform 移動
    //   （AnimationSequencerWindow::DrawRootMotionPreview）が二重に加算されてしまい、
    //   「アニメーションだけが Position より前に出てしまう」不具合の原因になる。
    //   ※ 実行時と異なり RootMotionComponent.enabled の判定が無いため、
    //     シーケンサーでの手動プレビューは常にピン留めする
    //     （このウィンドウは root motion を Transform 側で制御する用途が前提のため）。
    // ========================================================
    if (!animator->poseBufferA.empty())
    {
        const int rootNodeIndex = model->FindRealRootMotionNodeIndex(animIndex);
        PinRootMotionNode(model, animIndex, rootNodeIndex, animator->poseBufferA, animator->lockVerticalRootMotion);
    }

    model->SetNodePoses(animator->poseBufferA);
}

REGISTER_LOGIC_SYSTEM(AnimationSystem, Priority::LogicStage::L02_PostUpdate);