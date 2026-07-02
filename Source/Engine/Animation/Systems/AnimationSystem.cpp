#include "AnimationSystem.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include <cmath>

#include "Engine/Animation/Core/AnimEventMessages.h"
#include "Engine/Core/Math/CurveData.h"

#include "Engine/Graphics/Components/Core/ModelComponent.h"
#include "Engine/Graphics/Resource/ModelResource.h"
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
                        //   非ブレンド経路と同じく「真のルートボーン」のXZだけを0秒姿勢へ固定する。
                        //   ここを [0] 固定にしていると、rootNodeIndex が0番以外(通常Hips=1)の
                        //   リグでは遷移ブレンド中だけ焼き込み前進が漏れ、ブレンド終了で
                        //   その場へ戻る「前に出てすぐ戻る」不具合になる。
                        //   過去バッファは別クリップ(=別のルートボーン番号)なので個別に特定する。
                        //
                        //   ★ Y方向のピン基準について:
                        //   lockVerticalRootMotion 時は「そのクリップ自身の0秒目」ではなく
                        //   ModelResource のバインドポーズ（実行中に一切変化しない静的な基準姿勢）
                        //   を使う。攻撃クリップの0秒目が既に膝を曲げた姿勢から始まっていると、
                        //   そこを基準にした瞬間アニメ全体がその分浮く/沈むため
                        //   （"攻撃モーションで足が浮く"不具合の直接原因）。
                        const ModelResource *modelRes = model->GetResource();
                        if (!animator.poseBufferB.empty())
                        {
                            const int prevRootNodeIndex = model->FindRealRootMotionNodeIndex(prevAnimIndex);
                            if (prevRootNodeIndex >= 0 && prevRootNodeIndex < (int)animator.poseBufferB.size())
                            {
                                Model::NodePose initPoseB;
                                model->ComputeAnimation(prevAnimIndex, prevRootNodeIndex, 0.0f, initPoseB);
                                animator.poseBufferB[prevRootNodeIndex].position.x = initPoseB.position.x;
                                animator.poseBufferB[prevRootNodeIndex].position.z = initPoseB.position.z;
                                if (animator.lockVerticalRootMotion && modelRes &&
                                    prevRootNodeIndex < (int)modelRes->GetNodes().size())
                                    animator.poseBufferB[prevRootNodeIndex].position.y =
                                        modelRes->GetNodes()[prevRootNodeIndex].translate.y;
                            }
                        }
                        if (!animator.poseBufferA.empty())
                        {
                            if (rootNodeIndex >= 0 && rootNodeIndex < (int)animator.poseBufferA.size())
                            {
                                Model::NodePose initPoseA;
                                model->ComputeAnimation(animIndex, rootNodeIndex, 0.0f, initPoseA);
                                animator.poseBufferA[rootNodeIndex].position.x = initPoseA.position.x;
                                animator.poseBufferA[rootNodeIndex].position.z = initPoseA.position.z;
                                if (animator.lockVerticalRootMotion && modelRes &&
                                    rootNodeIndex < (int)modelRes->GetNodes().size())
                                    animator.poseBufferA[rootNodeIndex].position.y =
                                        modelRes->GetNodes()[rootNodeIndex].translate.y;
                            }
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

                        // Y(高さ)のアニメーションは既定では活かすが、lockVerticalRootMotion時は
                        // モデルのバインドポーズ（実行中不変の基準姿勢）へピン留めする。
                        // クリップ自身の0秒目を基準にすると、そこが既に膝を曲げた姿勢の場合
                        // アニメ全体がその分浮く/沈む（"攻撃で足が浮く"不具合の原因）ため、
                        // どのクリップでも同じ高さになるバインドポーズを基準にする。
                        if (animator.lockVerticalRootMotion)
                        {
                            const ModelResource *modelRes = model->GetResource();
                            if (modelRes && rootNodeIndex < (int)modelRes->GetNodes().size())
                                animator.poseBufferA[rootNodeIndex].position.y =
                                    modelRes->GetNodes()[rootNodeIndex].translate.y;
                        }
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
    if (animIndex == -1)
        return;

    model->ComputeAnimation(animIndex, time, animator->poseBufferA);

    // ========================================================
    // ★ Force Root Lock（エディタプレビュー版）
    //   実行時の Update() と同様に、ルートボーンのXZ移動をクリップの初期姿勢へ
    //   ピン留めする。これが無いと、クリップに焼き込まれたルートボーン自身の
    //   前進(スケルトンのアニメーション)と、RootMotionカーブ由来の Transform 移動
    //   （AnimationSequencerWindow::DrawRootMotionPreview）が二重に加算されてしまい、
    //   「アニメーションだけが Position より前に出てしまう」不具合の原因になる。
    //   Yは高さの変化（しゃがみ・ジャンプ等）を活かすためピン留めしない。
    //   ※ 実行時と異なり RootMotionComponent.enabled の判定が無いため、
    //     シーケンサーでの手動プレビューは常にピン留めする
    //     （このウィンドウは root motion を Transform 側で制御する用途が前提のため）。
    // ========================================================
    if (!animator->poseBufferA.empty())
    {
        const int rootNodeIndex = model->FindRealRootMotionNodeIndex(animIndex);

        std::vector<Model::NodePose> initPoses;
        model->ComputeAnimation(animIndex, 0.0f, initPoses);

        if (rootNodeIndex >= 0 &&
            rootNodeIndex < (int)initPoses.size() &&
            rootNodeIndex < (int)animator->poseBufferA.size())
        {
            animator->poseBufferA[rootNodeIndex].position.x = initPoses[rootNodeIndex].position.x;
            animator->poseBufferA[rootNodeIndex].position.z = initPoses[rootNodeIndex].position.z;

            // クリップ自身の0秒目ではなく、モデルのバインドポーズを高さの基準にする
            // （実機の Update() と同じ基準に揃え、プレビューと実機の見た目を一致させる）。
            if (animator->lockVerticalRootMotion)
            {
                const ModelResource *modelRes = model->GetResource();
                if (modelRes && rootNodeIndex < (int)modelRes->GetNodes().size())
                    animator->poseBufferA[rootNodeIndex].position.y =
                        modelRes->GetNodes()[rootNodeIndex].translate.y;
            }
        }
    }

    model->SetNodePoses(animator->poseBufferA);
}

REGISTER_LOGIC_SYSTEM(AnimationSystem, Priority::LogicStage::L02_PostUpdate);