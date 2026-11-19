#include "AnimationSystem.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include <algorithm>
#include <random>
#include <tracy/Tracy.hpp>

#include "Engine/Animation/Core/AnimEventMessages.h"
#include "Engine/Core/Math/CurveData.h"

#include "Engine/Graphics/Components/Core/ModelComponent.h"
#include "Engine/Animation/Components/AnimatorComponent.h"
#include "Engine/Time/Components/TimeState.h"

AnimationSystem::AnimationSystem() : IfSystem("AnimationSystem") {}



void AnimationSystem::Update(float dt)
{
    ZoneScopedN("AnimationSystem::Update");

    // 直列に回す。DispatchPointEvents が EventBus へ Publish するので、
    //   並列にするなら先に EventBus をスレッドセーフにすること
    ForEachWithID([this](CCL::ECS::EntityID id, AnimatorComponent &animator, const ModelComponent &,
                         const TimeStateComponent &time) {
        if (!animator.currentSequence) return;

        // エディタで操作中は時間を進めず、イベントも発火しない
        if (animator.isEditorOverride) return;

        const AnimSequence *seq = animator.currentSequence;

        // 再生時刻（ブレンド中は前のクリップの時刻も）を、速度カーブ込みで進める
        const bool movedForward = AdvanceTimers(animator, seq, time.localDt);

        // 今の時刻までに来た点イベント（音・エフェクト・判定球）を EventBus で出す
        DispatchPointEvents(id, animator, seq, movedForward);

        // 末尾を過ぎたら、ループなら先頭へ戻し、ループしないなら終了にする
        ResolveLoopAndFinish(animator, seq);
    });
}



void AnimationSystem::DispatchPointEvents(CCL::ECS::EntityID id, AnimatorComponent &animator,
                                          const AnimSequence *seq, bool movedForward)
{
    ZoneScopedN("AnimationSystem::DispatchPointEvents");

    if (!movedForward)
    {
        // 逆再生・シーク: 発火せず、イベントの位置だけ現在時刻に合わせる
        animator.SyncEventCursor();
        return;
    }

    // events は startTime 昇順なので、nextEventIndex から順に見て、まだ達していないところで止める
    while (animator.nextEventIndex < static_cast<int>(seq->events.size()))
    {
        const auto &evt = seq->events[animator.nextEventIndex];

        if (animator.currentTimer < evt.startTime)
            break;

        switch (evt.type)
        {
        case AnimEventType::Sound:
            // 効果音の再生を依頼する（AudioSystem が受ける）
            m_world->GetEventBus().Publish(AnimEventSoundMessage{id, evt.stringParam, evt.soundVolume,
                                                                 RandomizePitch(evt.soundPitch, evt.soundPitchRandom)});
            break;

        case AnimEventType::Effect:
            // VFX の生成を依頼する（VFXSystem が受ける）。継続放出の VFX はブロック長だけ出す
            m_world->GetEventBus().Publish(AnimEventEffectMessage{
                id, evt.stringParam, evt.offset, evt.effectScale, evt.effectFollow, evt.endTime - evt.startTime,
                evt.effectAttachTag});
            break;

        case AnimEventType::SpawnHitbox: {
            // ムービー（タイムライン）がクリップを握っている間は攻撃判定を出さない
            //   演出で攻撃モーションを見せただけで、近くのプレイヤーにダメージが入るため
            //   音とエフェクトは演出として欲しいので、上の Sound / Effect は止めない
            if (animator.isExternalOverride) break;

            // 生存時間はシーケンサーのブロック長
            //   点（startTime == endTime）なら 0 になり、購読側が最小の寿命を使う
            const float window = evt.endTime - evt.startTime;
            m_world->GetEventBus().Publish(AnimEventSpawnHitboxMessage{
                id, evt.offset, evt.radius, evt.damage, evt.hitStopDuration, evt.hitStopScale, window,
                evt.attackLayer, evt.knockback, evt.hitboxFollow});
            break;
        }

        default:
            // 区間のイベント（HitBox / Trail / 受付区間など）は出さない。各 System が再生時刻を見て判定する
            break;
        }

        animator.nextEventIndex++;
    }
}



bool AnimationSystem::AdvanceTimers(AnimatorComponent &animator, const AnimSequence *seq, float localDt)
{
    const float effectiveSpeed = ComputeEffectiveSpeed(animator, seq);
    const float prevTime       = animator.currentTimer;

    // 進める前の時刻を控える（区間イベントの判定に使う）
    animator.previousFrameTimer = prevTime;

    animator.currentTimer += localDt * effectiveSpeed;
    const bool movedForward = (animator.currentTimer >= prevTime);

    // --- 前のクリップ（ブレンド中だけ）---
    if (animator.isBlending)
    {
        animator.currentBlendTime += localDt;

        // 前のクリップは切り替え前の設定（速度・ループ）で進める
        animator.previousTimer += localDt * animator.previousPlaybackSpeed;

        const AnimSequence *prevSeq = animator.previousSequence;
        if (prevSeq) animator.previousTimer = CCL::Anim::WrapClipTime(animator.previousTimer, prevSeq->duration, animator.previousLoop);

        if (animator.currentBlendTime >= animator.blendDuration)
        {
            animator.isBlending       = false;
            animator.previousSequence = nullptr;
        }
    }

    return movedForward;
}



float AnimationSystem::ComputeEffectiveSpeed(const AnimatorComponent &animator, const AnimSequence *seq)
{
    // ここに Tracy のゾーンを置かないこと
    //   処理は数十ナノ秒でゾーン自体のコストと同程度になり、OS のプリエンプトを拾って巨大な値が出る
    float curveMultiplier = 1.0f;
    if (const CurveData *curve = animator.GetSpeedCurve())
    {
        float progress = (seq->duration > 0.0f) ? (animator.currentTimer / seq->duration) : 0.0f;

        // ループの境界をまたいだステップでは progress > 1 になり、速度カーブが外挿されて一瞬跳ねる
        //   [0,1] に丸めて防ぐ
        progress        = std::clamp(progress, 0.0f, 1.0f);
        curveMultiplier = curve->Evaluate(progress);
    }
    return animator.playbackSpeed * animator.footSlideMultiplier * curveMultiplier;
}



void AnimationSystem::ResolveLoopAndFinish(AnimatorComponent &animator, const AnimSequence *seq)
{
    if (seq->duration <= 0.0f) return;

    if (animator.currentTimer >= seq->duration)
    {
        if (animator.isLoop)
        {
            animator.currentTimer   = CCL::Anim::WrapClipTime(animator.currentTimer, seq->duration, true);
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
            animator.currentTimer = CCL::Anim::WrapClipTime(animator.currentTimer, seq->duration, true);
            animator.SyncEventCursor();
        }
        else
        {
            animator.currentTimer = 0.0f;
        }
    }
}



float AnimationSystem::RandomizePitch(float pitch, float range)
{
    if (range <= 0.0f) return pitch;

    // 乱数の状態は thread_local に持つ（呼び出し元を並列にしても状態を共有しない）
    thread_local std::minstd_rand rng{std::random_device{}()};
    std::uniform_real_distribution<float> dist(-range, range);
    return pitch + dist(rng);
}



REGISTER_LOGIC_SYSTEM(AnimationSystem, Priority::LogicStage::L02_PostUpdate);
