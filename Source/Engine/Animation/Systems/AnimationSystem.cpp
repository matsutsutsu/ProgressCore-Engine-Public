#include "AnimationSystem.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include <cmath>
#include <algorithm>
#include <random>
#include <tracy/Tracy.hpp>

#include "Engine/Animation/Core/AnimEventMessages.h"
#include "Engine/Animation/Core/AnimPoseUtils.h"
#include "Engine/Core/Math/CurveData.h"

#include "Engine/Graphics/Components/Core/ModelComponent.h"
#include "Engine/Graphics/Resource/ModelResource.h"
#include "Engine/Animation/Components/AnimatorComponent.h"
#include "Engine/Animation/Components/RootMotionComponent.h"
#include "Engine/Time/Components/TimeState.h"

// m_world を使わない時間の計算は、ここに置く
namespace
{
    // 効果音のピッチに、±range の揺らぎを足して返す
    //   乱数の状態は thread_local に持つ（呼び出し元を並列にしても状態を共有しない）
    float RandomizePitch(float pitch, float range)
    {
        if (range <= 0.0f) return pitch;
        thread_local std::minstd_rand rng{std::random_device{}()};
        std::uniform_real_distribution<float> dist(-range, range);
        return pitch + dist(rng);
    }



    // 実効の再生速度（ステートの速度 × 足滑り補正 × 速度カーブ）を返す
    float ComputeEffectiveSpeed(const AnimatorComponent &animator, const AnimSequence *seq)
    {
        // ここに Tracy のゾーンを置かないこと
        //   処理は数十ナノ秒でゾーン自体のコストと同程度になり、OS のプリエンプトを拾って巨大な値が出る
        float curveMultiplier = 1.0f;
        if (animator.activeCurve)
        {
            float progress = (seq->duration > 0.0f) ? (animator.currentTimer / seq->duration) : 0.0f;

            // ループの境界をまたいだステップでは progress > 1 になり、速度カーブが外挿されて一瞬跳ねる
            //   [0,1] に丸めて防ぐ
            progress        = std::clamp(progress, 0.0f, 1.0f);
            curveMultiplier = animator.activeCurve->Evaluate(progress);
        }
        return animator.playbackSpeed * animator.footSlideMultiplier * curveMultiplier;
    }



    // currentTimer（ブレンド中なら previousTimer も）を進める。順方向に進んだら true
    bool AdvanceTimers(AnimatorComponent &animator, const AnimSequence *seq, float localDt)
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

            // 前のクリップは切り替え前の速度で進める
            animator.previousTimer += localDt * animator.previousPlaybackSpeed;

            if (animator.previousSequence && animator.previousSequence->duration > 0.0f)
            {
                if (!animator.previousLoop)
                {
                    // 非ループ: duration を超えたら末尾で止める
                    animator.previousTimer = (std::min)(animator.previousTimer, animator.previousSequence->duration);
                }
                else
                {
                    // ループ: 剰余でループ内の時刻に戻す
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



    // duration の境界でループ・終了を判定し、currentTimer をクリップ内に収める
    void ResolveLoopAndFinish(AnimatorComponent &animator, const AnimSequence *seq)
    {
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

AnimationSystem::AnimationSystem() : IfSystem("AnimationSystem") {}



void AnimationSystem::Update(float dt)
{
    // 処理フロー:
    // 1. 時間を進める（速度カーブを含む）
    // 2. 点イベントを発火する
    // 3. ループと終了を判定する

    // 2 の後に 3 を行うので、ループ末尾ちょうどのイベントも、境界をまたいだステップで発火できる
    // ポーズの計算は AnimationPoseSystem が 1 描画フレームに 1 回行う

    ZoneScopedN("AnimationSystem::Update");

    // 直列に回す。DispatchPointEvents が EventBus へ Publish するので、
    //   並列にするなら先に EventBus をスレッドセーフにすること
    //   ModelComponent は Model を持つキャラだけに絞るために受け取っているだけで、ここでは触らない
    ForEachWithID([this](CCL::ECS::EntityID id, AnimatorComponent &animator, ModelComponent &modelComp, const TimeStateComponent &time) {
        if (!animator.currentSequence) return;

        // エディタで操作中は時間を進めず、イベントも発火しない
        if (animator.isEditorOverride) return;

        const AnimSequence *seq = animator.currentSequence;

        const bool movedForward = AdvanceTimers(animator, seq, time.localDt);

        DispatchPointEvents(id, animator, seq, movedForward);

        ResolveLoopAndFinish(animator, seq);

        (void)modelComp;
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

        case AnimEventType::HitBox:
        case AnimEventType::Trail:
        case AnimEventType::Particle:
        case AnimEventType::Blackboard:
        case AnimEventType::Finisher:
            // 区間のイベント（HitBox / Trail / Particle など）は出さない。各 System が再生時刻を見て判定する
            break;

        default:
            break;
        }

        animator.nextEventIndex++;
    }
}



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

    // Force Root Lock（プレビュー版）。RootMotionComponent の有無に関係なく常に固定する
    //   固定しないと、クリップ自身の前進と、ルートモーションカーブによる Transform の移動
    //   （AnimationSequencerWindow::DrawRootMotionPreview）が二重に掛かり、モデルだけ前に出る
    if (!animator->poseBufferA.empty())
    {
        const int rootNodeIndex = model->FindRealRootMotionNodeIndex(animIndex);
        PinRootMotionNode(model, animIndex, rootNodeIndex, animator->poseBufferA, animator->lockVerticalRootMotion);
    }

    model->SetNodePoses(animator->poseBufferA);
}



REGISTER_LOGIC_SYSTEM(AnimationSystem, Priority::LogicStage::L02_PostUpdate);
