#include "AnimationSystem.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include <cmath>
#include "Engine/Core/Math/StringHash.h"

#include "Engine/Animation/Core/AnimEventMessages.h"
#include "Engine/Animation/Core/AnimationCurve.h"

#include "Engine/Graphics/Components/Core/ModelComponent.h"
#include "Engine/Animation/Components/AnimatorComponent.h"
#include "Engine/Timer/TimeState.h"

// ===================================================================================
// ファイル: AnimationSystem.cpp
// 概要: アニメーションの「時間進行」と「イベント発火」を担う実働システム
// ===================================================================================

AnimationSystem::AnimationSystem()
    : IfSystem("AnimationSystem")
{
}

void AnimationSystem::Update(float dt)
{
    ForEachWithID([this](CCL::ECS::EntityID id, AnimatorComponent &animator, ModelComponent &modelComp, const TimeStateComponent &time) {
        // 台本（Sequence）がセットされていなければ何もしない
        if (!animator.currentSequence)
            return;

        // エディタで操作中の場合は、通常の時間進行とイベント発火をストップする
        if (animator.isEditorOverride)
            return;

        const AnimSequence *seq = animator.currentSequence;

        // ========================================================
        // 1. カーブによる時間の歪み（Time Warping）計算
        // ========================================================
        float curveMultiplier = 1.0f;
        if (animator.activeCurve)
        {
            // duration が 0 のときの 0除算/NaN を回避
            float progress  = (seq->duration > 0.0f) ? (animator.currentTimer / seq->duration) : 0.0f;
            curveMultiplier = animator.activeCurve->Evaluate(progress);
        }

        const float effectiveSpeed = animator.playbackSpeed * curveMultiplier;
        const float prevTime       = animator.currentTimer;

        // 時間を進行させる
        animator.currentTimer += time.localDt * effectiveSpeed;

        // 順方向に進んだか（逆再生・シーク検出用）
        const bool movedForward = (animator.currentTimer >= prevTime);

        // ========================================================
        //  過去の記憶（ブレンド）のタイマーを進行させる
        // ========================================================
        if (animator.isBlending)
        {
            animator.currentBlendTime += time.localDt;
            animator.previousTimer += time.localDt * animator.playbackSpeed;

            if (animator.previousSequence && animator.previousSequence->duration > 0.0f)
            {
                animator.previousTimer = fmod(animator.previousTimer, animator.previousSequence->duration);
            }

            if (animator.currentBlendTime >= animator.blendDuration)
            {
                animator.isBlending       = false;
                animator.previousSequence = nullptr;
            }
        }

        // ========================================================
        // 2. 高速イベント発火ロジック (O(1)アクセス)
        //    ※ 順方向再生時のみ発火。逆再生時は栞を現在時刻へ再同期する。
        // ========================================================
        if (movedForward)
        {
            while (animator.nextEventIndex < static_cast<int>(seq->events.size()))
            {
                const auto &evt = seq->events[animator.nextEventIndex];

                // まだイベントの時間に達していなければ、これ以上の走査は不要
                if (animator.currentTimer >= evt.startTime)
                {

                    // 文字列を数値に変換し、switch文で最速のジャンプを行う
                    uint32_t eventHash = CCL::Utils::HashString(evt.eventName.c_str());

                    switch (eventHash)
                    {
                    case CCL::Utils::HashString("HitBox_Start"):
                        _world->GetEventBus().Publish(AnimEventHitBoxMessage{id, true});
                        break;
                    case CCL::Utils::HashString("HitBox_End"):
                        _world->GetEventBus().Publish(AnimEventHitBoxMessage{id, false});
                        break;
                    case CCL::Utils::HashString("Play_Sound"):
                        _world->GetEventBus().Publish(AnimEventSoundMessage{id});
                        break;
                    case CCL::Utils::HashString("Play_Effect"):
                        _world->GetEventBus().Publish(AnimEventEffectMessage{id, evt.stringParam});
                        break;
                    case CCL::Utils::HashString("HitBox"):
                        // [重要] Hitbox は CombatAnimationSyncSystem が直接「状態」として
                        // 読み取って処理するため、イベントバスに投げる必要はない。無視してよい。
                        break;
                    default:
                        // 未知のイベントは無視
                        break;
                    }

                    animator.nextEventIndex++;
                }
                else
                {
                    break; // まだ時間が来ていないイベントに当たったらループを抜ける
                }
            }
        }
        else
        {
            // 逆再生・シーク: 発火せず栞だけ現在時刻に合わせる
            animator.SyncEventCursor();
        }

        // ========================================================
        // 3. ループと終了の判定（duration>0 のときのみ）
        // ========================================================
        if (seq->duration > 0.0f)
        {
            if (animator.currentTimer >= seq->duration)
            {
                if (animator.isLoop)
                {
                    // ループ時: 時間を丸め込み、イベントの栞を最初に戻す
                    animator.currentTimer   = fmod(animator.currentTimer, seq->duration);
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
                // 逆再生で先頭を下回った場合
                if (animator.isLoop)
                {
                    animator.currentTimer = fmod(animator.currentTimer, seq->duration) + seq->duration;
                    animator.SyncEventCursor();
                }
                else
                {
                    animator.currentTimer = 0.0f;
                }
            }
        }

        // ========================================================
        // 4. 描画側(Model)への計算依頼
        // ========================================================
        if (modelComp.GetModel())
        {
            Model *model     = modelComp.GetModel();
            int    animIndex = model->GetAnimationIndex(seq->targetAnimName.c_str());

            if (animIndex != -1)
            {
                // ブレンド中の場合は2つのアニメーションを計算して混ぜる
                if (animator.isBlending && animator.previousSequence)
                {
                    int prevAnimIndex = model->GetAnimationIndex(animator.previousSequence->targetAnimName.c_str());

                    if (prevAnimIndex != -1)
                    {
                        // ① 過去の姿勢を B に保存
                        model->ComputeAnimation(prevAnimIndex, animator.previousTimer, animator.poseBufferB);
                        // ② 現在の姿勢を A に保存
                        model->ComputeAnimation(animIndex, animator.currentTimer, animator.poseBufferA);

                        // ③ 補間率 (0.0 ~ 1.0) を算出してクランプ
                        float blendRate = (animator.blendDuration > 0.0f)
                                              ? (animator.currentBlendTime / animator.blendDuration)
                                              : 1.0f;
                        blendRate       = std::fmax(0.0f, std::fmin(blendRate, 1.0f));

                        // ④ BとAを混ぜ合わせ、結果を A に上書きする
                        Model::BlendAnimations(animator.poseBufferB, animator.poseBufferA, blendRate, animator.poseBufferA);

                        // ⑤ 混ぜた結果を骨に適用
                        model->SetNodePoses(animator.poseBufferA);
                    }
                    else
                    {
                        // 過去のデータが見つからなければ通常再生
                        model->ComputeAnimation(animIndex, animator.currentTimer, animator.poseBufferA);
                        model->SetNodePoses(animator.poseBufferA);
                    }
                }
                else
                {
                    // ★ 通常の単独再生
                    model->ComputeAnimation(animIndex, animator.currentTimer, animator.poseBufferA);
                    model->SetNodePoses(animator.poseBufferA);
                }
            }
        }
    });
}

// エディタ（AnimationSequencerWindow）から呼ばれる手動更新処理
void AnimationSystem::UpdateManual(CCL::ECS::EntityID entity, float time)
{
    auto *animator  = _world->GetComponent<AnimatorComponent>(entity);
    auto *modelComp = _world->GetComponent<ModelComponent>(entity);

    if (!animator || !modelComp || !modelComp->GetModel())
        return;

    Model *model = modelComp->GetModel();

    const AnimSequence *seq = animator->currentSequence;
    if (!seq)
        return;

    int animIndex = model->GetAnimationIndex(seq->targetAnimName.c_str());

    if (animIndex != -1)
    {
        model->ComputeAnimation(animIndex, time, animator->poseBufferA);
        model->SetNodePoses(animator->poseBufferA);
    }
}

REGISTER_LOGIC_SYSTEM(AnimationSystem, Priority::LogicStage::L02_Update);