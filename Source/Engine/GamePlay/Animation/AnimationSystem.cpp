#include "AnimationSystem.h"

// 実装に必要なヘッダ
#include "ECS/Core/CCL_World.h"
#include "Engine/Graphics/Resource/Model.h"

// システムの実行順序の定義ヘッダー
#include "ECS/System/CCL_SystemRegistry.h"
#include "Game/Core/SystemPriority.h"

#include <cmath> // fmod用

AnimationSystem::AnimationSystem() : IfSystem("AnimationSystem") {}

void AnimationSystem::Update(float dt)
{
    // 全エンティティのアニメーションを処理
    ForEachParallel([dt](AnimatorComponent &animator, ModelComponent &modelComp) {
        if (!modelComp.modelPtr) return;
        Model      *model    = modelComp.modelPtr.get();
        const auto *resource = model->GetResource();
        if (!resource) return;

        const auto &animations = resource->GetAnimations();

        // --- 0. イベントリストのクリア ---
        animator.triggeredEvents.clear();
        animator.activeEvents.clear();

        // --- 1. 時間の進行処理 ---

        // 現在のアニメーションの進行
        if (animator.currentAnimIndex >= 0 && animator.currentAnimIndex < (int)animations.size()) {

            const auto &animData = animations[animator.currentAnimIndex];

            // 更新前の時間を保存
            animator.prevTimer = animator.currentTimer;

            // 時間進行 (既存処理)
            if (!animator.isFinished) {
                float duration = animations[animator.currentAnimIndex].secondsLength;
                animator.currentTimer += dt * animator.playbackSpeed; // 60fps基準の補正

                // ループ・終了判定
                if (animator.currentTimer >= duration) {
                    if (animator.isLoop) {
                        animator.currentTimer = fmod(animator.currentTimer, duration);

                        // ループした瞬間、prevTimerをリセット扱いにすることで、
                        // 次の判定で「末尾 ~ 先頭」のイベントを拾えるようにする（簡易対応）
                        animator.prevTimer = -0.0001f;
                    }
                    else {
                        animator.currentTimer = duration;
                        // 遷移中でなければ終了フラグ
                        if (animator.targetAnimIndex == -1) {
                            animator.isFinished = true;
                        }
                    }
                }
            }

            for (const auto &evt : animData.events) {
                // A. 瞬間イベント (Sound, Effect)
                // 「前回 <= イベント時間 < 今回」 の区間にあれば発火
                if (animator.prevTimer <= evt.time && animator.currentTimer > evt.time) {
                    animator.triggeredEvents.push_back(evt);
                }

                // B. 持続イベント (HitBox)
                // 「現在時間が [開始, 終了] の中」にあればアクティブ
                float endTime = evt.time + evt.duration;
                if (animator.currentTimer >= evt.time && animator.currentTimer <= endTime) {
                    animator.activeEvents.push_back(evt);
                }
            }
        }

        // 遷移先がある場合の処理
        if (animator.targetAnimIndex >= 0 && animator.targetAnimIndex < (int)animations.size()) {
            float targetDuration = animations[animator.targetAnimIndex].secondsLength;

            // 遷移タイマー進行
            animator.transitionTimer += dt;

            // ターゲットのアニメーションタイマーも進める
            animator.targetTimer += dt * animator.playbackSpeed;
            if (animator.isLoop) {
                animator.targetTimer = fmod(animator.targetTimer, targetDuration);
            }
            else if (animator.targetTimer >= targetDuration) {
                animator.targetTimer = targetDuration;
            }

            // 遷移完了判定
            if (animator.transitionTimer >= animator.transitionDuration) {
                // 遷移完了：TargetをCurrentに格上げする
                animator.currentAnimIndex = animator.targetAnimIndex;
                animator.currentTimer     = animator.targetTimer;

                animator.targetAnimIndex = -1;
                animator.transitionTimer = 0.0f;
                animator.targetTimer     = 0.0f;
            }
        }

        // --- 2. 姿勢の計算 (Compute) ---

        bool hasCurrent = (animator.currentAnimIndex >= 0);
        bool hasTarget  = (animator.targetAnimIndex >= 0);

        // A. 何も再生していない
        if (!hasCurrent && !hasTarget) return;

        // B. ブレンディング中 (Current -> Target)
        if (hasCurrent && hasTarget) {
            // 1. Currentの姿勢を計算
            model->ComputeAnimation(
                animator.currentAnimIndex, animator.currentTimer, animator.poseBufferA);
            // 2. Targetの姿勢を計算
            model->ComputeAnimation(
                animator.targetAnimIndex, animator.targetTimer, animator.poseBufferB);

            // 3. ブレンド率 (0.0 = Current, 1.0 = Target)
            float blendRate = animator.transitionTimer / animator.transitionDuration;
            if (blendRate > 1.0f) blendRate = 1.0f;

            // 4. 合成
            Model::BlendAnimations(
                animator.poseBufferA, animator.poseBufferB, blendRate, animator.finalPoseBuffer);

            // 適用
            model->SetNodePoses(animator.finalPoseBuffer);
        }
        // C. 単体再生 (Currentのみ)
        else if (hasCurrent) {
            // 直接 finalPoseBuffer に計算結果を入れてもらう（コピー削減）
            model->ComputeAnimation(
                animator.currentAnimIndex, animator.currentTimer, animator.finalPoseBuffer);

            // 適用
            model->SetNodePoses(animator.finalPoseBuffer);
        }
    });
}

// エディタ用の手動更新関数
void AnimationSystem::UpdateManual(CCL::ECS::EntityID entity, float time)
{
    using namespace CCL::ECS;
    auto *animator  = _world->GetComponent<AnimatorComponent>(entity);
    auto *modelComp = _world->GetComponent<ModelComponent>(entity);

    if (animator && modelComp && modelComp->modelPtr) {
        Model      *model    = modelComp->modelPtr.get();
        const auto *resource = model->GetResource(); // リソース取得

        // 1. 時間の更新
        animator->prevTimer    = animator->currentTimer;
        animator->currentTimer = time;

        // 2. イベント判定
        animator->triggeredEvents.clear();
        animator->activeEvents.clear();

        if (resource) {
            const auto &animations = resource->GetAnimations();
            if (animator->currentAnimIndex >= 0 &&
                animator->currentAnimIndex < (int)animations.size()) {
                const auto &animData = animations[animator->currentAnimIndex];

                for (const auto &evt : animData.events) {
                    // B. 持続イベント (HitBox)
                    float endTime = evt.time + evt.duration;
                    if (animator->currentTimer >= evt.time && animator->currentTimer <= endTime) {
                        animator->activeEvents.push_back(evt);
                    }

                    // A. 瞬間イベント (Sound)
                    if (animator->currentTimer > animator->prevTimer) {
                        if (animator->prevTimer <= evt.time && animator->currentTimer > evt.time) {
                            animator->triggeredEvents.push_back(evt);
                        }
                    }
                }
            }
        }

        // 3. 姿勢計算と適用
        model->ComputeAnimation(
            animator->currentAnimIndex, animator->currentTimer, animator->poseBufferA);
        model->SetNodePoses(animator->poseBufferA);
    }
}

// ==========================================
// マクロは必ず .cpp の末尾に1回だけ書く
// ==========================================
REGISTER_RENDER_SYSTEM(AnimationSystem, Priority::RenderStage::Animation);