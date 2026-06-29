#include "AnimationSystem.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include <cmath>

#include "Engine/Animation/Core/AnimEventMessages.h"
#include "Engine/Animation/Core/AnimationCurve.h"

#include "Engine/Graphics/Components/Core/ModelComponent.h"
#include "Engine/Animation/Components/AnimatorComponent.h"
#include "Engine/Animation/Components/RootMotionComponent.h"
#include "Engine/Timer/TimeState.h"

// ===================================================================================
// ファイル: AnimationSystem.cpp
// 概要: アニメーションの「時間進行」と「イベント発火」を担う実働システム
//
// [ イベント発火の方針 ]
//   点イベント(Sound/Effect/SpawnHitbox) は EventBus で1回だけ発火。
//   区間状態イベント(HitBox/Trail/Blackboard) は発火せず、各専用システムが
//   区間ポーリングで読み取る（AnimEventType で型安全に分岐）。
// ===================================================================================

AnimationSystem::AnimationSystem()
    : IfSystem("AnimationSystem")
{
}

void AnimationSystem::Update(float dt)
{
    // 直列実行。Model は共有インスタンスを書き換える(SetNodePoses)ため、
    // 安易な並列化はポーズの競合を招く。並列化する場合は Model のポーズ書き込みを
    // エンティティ単位に分離してから行うこと。
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

            // 過去のモーションは「過去のスピード」で進める
            animator.previousTimer += time.localDt * animator.previousPlaybackSpeed;

            if (animator.previousSequence && animator.previousSequence->duration > 0.0f)
            {
                // ループ設定に応じて「フリーズ」か「ループ」かを分ける
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
                    {
                        animator.previousTimer += animator.previousSequence->duration;
                    }
                }
            }

            if (animator.currentBlendTime >= animator.blendDuration)
            {
                animator.isBlending       = false;
                animator.previousSequence = nullptr;
            }
        }

        // ========================================================
        // 2. イベント発火ロジック (O(1)栞アクセス)
        //    点イベントのみ発火。区間状態イベントは各専用システムが読む。
        // ========================================================
        if (movedForward)
        {
            while (animator.nextEventIndex < static_cast<int>(seq->events.size()))
            {
                const auto &evt = seq->events[animator.nextEventIndex];

                // まだイベントの時間に達していなければ走査終了
                if (animator.currentTimer >= evt.startTime)
                {
                    switch (evt.type)
                    {
                    case AnimEventType::Sound:
                        // 点イベント: FMOD 再生を依頼
                        _world->GetEventBus().Publish(
                            AnimEventSoundMessage{id, evt.stringParam});
                        break;

                    case AnimEventType::Effect:
                        // 点イベント: Effekseer 生成を依頼（オフセット/スケール/追従を渡す）
                        _world->GetEventBus().Publish(
                            AnimEventEffectMessage{
                                id, evt.stringParam, evt.offset,
                                evt.effectScale, evt.effectFollow});
                        break;

                    case AnimEventType::SpawnHitbox:
                        // 点イベント: 空間に独立した球状ヒットボックスを生成依頼
                        _world->GetEventBus().Publish(
                            AnimEventSpawnHitboxMessage{
                                id, evt.offset, evt.radius,
                                evt.damage, evt.hitStopDuration});
                        break;

                    case AnimEventType::HitBox:
                    case AnimEventType::Trail:
                    case AnimEventType::Blackboard:
                        // 区間状態イベント: 専用システムが区間ポーリングで読むため発火しない。
                        //   HitBox      → CombatAnimationSyncSystem
                        //   Trail       → (Trail制御システム)
                        //   Blackboard  → (AIのBB更新システム / 現状未実装)
                        break;

                    default:
                        break;
                    }

                    animator.nextEventIndex++;
                }
                else
                {
                    break; // まだ時間が来ていないイベントに当たったら抜ける
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
                // ★ 数学的アプローチによる真のルートボーン特定
                const int rootNodeIndex = model->FindRealRootMotionNodeIndex(animIndex);

                if (animator.isBlending && animator.previousSequence)
                {
                    int prevAnimIndex = model->GetAnimationIndex(animator.previousSequence->targetAnimName.c_str());
                    if (prevAnimIndex != -1)
                    {
                        // バッファサイズの同期
                        size_t nodeCount = animator.poseBufferA.size();
                        if (animator.poseBufferB.size() != nodeCount)
                        {
                            animator.poseBufferB.resize(nodeCount);
                        }

                        // 過去と現在のポーズを計算
                        model->ComputeAnimation(prevAnimIndex, animator.previousTimer, animator.poseBufferB);
                        model->ComputeAnimation(animIndex, animator.currentTimer, animator.poseBufferA);

                        // ========================================================
                        // ★ Force Root Lock (ブレンド中の過去・現在両方のピン留め)
                        // ========================================================
                        if (!animator.poseBufferB.empty())
                        {
                            Model::NodePose initPoseB;
                            model->ComputeAnimation(prevAnimIndex, rootNodeIndex, 0.0f, initPoseB);
                            // X, Y, Z すべてを「0秒時点の初期位置」に固定し、アニメ自身の移動を完全に殺す
                            animator.poseBufferB[0].position = initPoseB.position;
                        }
                        if (!animator.poseBufferA.empty())
                        {
                            Model::NodePose initPoseA;
                            model->ComputeAnimation(animIndex, rootNodeIndex, 0.0f, initPoseA);
                            animator.poseBufferA[0].position = initPoseA.position;
                        }

                        float blendRate = (animator.blendDuration > 0.0f) ? (animator.currentBlendTime / animator.blendDuration) : 1.0f;
                        blendRate       = std::fmax(0.0f, std::fmin(blendRate, 1.0f));

                        // クリーンアップされたポーズ同士をブレンド
                        Model::BlendAnimations(animator.poseBufferB, animator.poseBufferA, blendRate, animator.poseBufferA);
                    }
                    else
                    {
                        model->ComputeAnimation(animIndex, animator.currentTimer, animator.poseBufferA);
                    }
                }
                else
                {
                    // ブレンドしていない通常再生
                    model->ComputeAnimation(animIndex, animator.currentTimer, animator.poseBufferA);
                }

                // ========================================================
                // ★ Force Root Lock (真のルートボーンのXZを初期位置にピン留め)
                // ========================================================
                auto *rootMotion = _world->GetComponent<RootMotionComponent>(id);
                if (rootMotion && rootMotion->enabled && !animator.poseBufferA.empty())
                {
                    // 配列全体を0秒で計算して初期ポーズ群を取得
                    std::vector<Model::NodePose> initPoses;
                    model->ComputeAnimation(animIndex, 0.0f, initPoses);

                    if (rootNodeIndex < initPoses.size() && rootNodeIndex < animator.poseBufferA.size())
                    {
                        // Y(高さ)のアニメーションはそのまま活かし、XZ(前後左右)の移動のみを殺す！
                        animator.poseBufferA[rootNodeIndex].position.x = initPoses[rootNodeIndex].position.x;
                        animator.poseBufferA[rootNodeIndex].position.z = initPoses[rootNodeIndex].position.z;
                    }
                }

                // 最終確定したポーズを行列へ反映（SetNodePosesはここに1回だけ！）
                model->SetNodePoses(animator.poseBufferA);
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

REGISTER_LOGIC_SYSTEM(AnimationSystem, Priority::LogicStage::L02_PostUpdate);