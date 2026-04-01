#include "AnimationSystem.h"
#include "ECS/Core/CCL_World.h"
//#include "Engine/Graphics/Resource/Model.h"
#include "ECS/System/CCL_SystemRegistry.h"
#include "Game/Core/SystemPriority.h"
#include <cmath>
#include "Engine/Core/Math/StringHash.h"

// 先ほど作成した専用の手紙ヘッダー
#include "AnimEventMessages.h"



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

            // ここがAAAの設計！
            // 文字列を数値に変換し、switch文で最速のジャンプを行う
            // ※実行時にはただの「数値の比較（int == int）」になるため負荷はゼロに等しい
            uint32_t eventHash = CCL::Utils::HashString(evt.eventName.c_str());

            switch (eventHash) {
            case  CCL::Utils::HashString("HitBox_Start"):
                _world->GetEventBus().Publish(AnimEventHitBoxMessage{ id, true });
                break;
            case  CCL::Utils::HashString("HitBox_End"):
                _world->GetEventBus().Publish(AnimEventHitBoxMessage{ id, false });
                break;
            case  CCL::Utils::HashString("Play_Sound"):
                _world->GetEventBus().Publish(AnimEventSoundMessage{ id });
                break;
            case  CCL::Utils::HashString("Play_Effect"):
                _world->GetEventBus().Publish(AnimEventEffectMessage{ id, evt.stringParam });
                break;
            default:
                // 未知のイベントは無視するか、警告を出す
                break;
            }

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