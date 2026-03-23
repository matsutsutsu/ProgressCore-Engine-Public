#include "AnimationSystem.h"
#include "ECS/Core/CCL_World.h"
//#include "Engine/Graphics/Resource/Model.h"
#include "ECS/System/CCL_SystemRegistry.h"
#include "Game/Core/SystemPriority.h"
#include <cmath>

// イベントを他システムへ伝えるための手紙（必要なら別ヘッダに切り出してください）
struct AnimNotifyMessage {
    CCL::ECS::EntityID entity;
    std::string eventName;
};

AnimationSystem::AnimationSystem() : IfSystem("AnimationSystem") {}

void AnimationSystem::Update(float dt)
{
    // ★超並列処理: ECSの真骨頂。数千体の計算を全コアに分散させる
    ForEachWithIDParallel([dt, this](CCL::ECS::EntityID id, AnimatorComponent& animator, ModelComponent& modelComp) {

        // 台本（Sequence）がセットされていなければ何もしない
        if (!animator.currentSequence) return;

        const AnimSequence* seq = animator.currentSequence;

        // 1. 時間を進行させる
        animator.currentTimer += dt * animator.playbackSpeed;

        // ========================================================
        // 2. 高速イベント発火ロジック (O(1)アクセス)
        // ========================================================
        while (animator.nextEventIndex < seq->events.size()) {
            const auto& evt = seq->events[animator.nextEventIndex];

            // まだイベントの時間に達していなければ、これ以上の走査は不要
            if (animator.currentTimer < evt.time) {
                break;
            }

            // 時間が来たので EventBus に「放送」する
            AnimNotifyMessage msg;
            msg.entity = id;
            msg.eventName = evt.eventName; // "HitBox_Start" 等
            _world->GetEventBus().Publish(msg);

            // 栞を次に進める
            animator.nextEventIndex++;
        }

        // ========================================================
        // 3. ループと終了の判定
        // ========================================================
        if (animator.currentTimer >= seq->duration) {
            if (animator.isLoop) {
                // ループ時: 時間を丸め込み、イベントの栞を最初に戻す！
                animator.currentTimer = fmod(animator.currentTimer, seq->duration);
                animator.nextEventIndex = 0;
            }
            else {
                animator.currentTimer = seq->duration;
                animator.isFinished = true;
            }
        }

        // ========================================================
        // 4. 描画側(Model)への計算依頼
        // ========================================================
        if (modelComp.modelPtr) {
            Model* model = modelComp.modelPtr.get();

            // 台本に書かれている「アニメーション名」から、Model内部のインデックスを引く
            int animIndex = model->GetAnimationIndex(seq->targetAnimName.c_str());

            if (animIndex != -1) {
                // 骨の姿勢を計算させ、キャッシュ(poseBufferA)に保存・適用する
                model->ComputeAnimation(animIndex, animator.currentTimer, animator.poseBufferA);
                model->SetNodePoses(animator.poseBufferA);
            }
        }
        });
}


// エディタ（AnimationSequencerWindow）から呼ばれる手動更新処理
void AnimationSystem::UpdateManual(CCL::ECS::EntityID entity, float time)
{
    // 1. 対象のエンティティから必要なコンポーネントを取得
    auto* animator = _world->GetComponent<AnimatorComponent>(entity);
    auto* modelComp = _world->GetComponent<ModelComponent>(entity);

    if (!animator || !modelComp || !modelComp->modelPtr) return;
    
    Model* model = modelComp->modelPtr.get();
    
    // 2. 現在読んでいる台本（エディタで編集中）を取得
    const AnimSequence* seq = animator->currentSequence;
    if (!seq) return;

    // 3. 台本に書かれているアニメーション名から、3Dモデル側のインデックスを引く
    int animIndex = model->GetAnimationIndex(seq->targetAnimName.c_str());
    
    // 4. 指定された時間（time）で骨の姿勢を計算し、モデルに適用する
    if (animIndex != -1) {
        model->ComputeAnimation(animIndex, time, animator->poseBufferA);
        model->SetNodePoses(animator->poseBufferA);
    }
}

REGISTER_LOGIC_SYSTEM(AnimationSystem, Priority::RenderStage::Animation);