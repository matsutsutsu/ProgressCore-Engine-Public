#include "AnimationPoseSystem.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include <algorithm>
#include <tracy/Tracy.hpp>

#include "Engine/Animation/Core/AnimPoseUtils.h"
#include "Engine/Animation/Core/AnimSequence.h"

#include "Engine/Graphics/Components/Core/ModelComponent.h"
#include "Engine/Graphics/Resource/ModelResource.h"
#include "Engine/Animation/Components/AnimatorComponent.h"
#include "Engine/Animation/Components/RootMotionComponent.h"

using CCL::Anim::PinRootMotionNode;

AnimationPoseSystem::AnimationPoseSystem()
    : IfSystem("AnimationPoseSystem")
{
}

void AnimationPoseSystem::Update(float dt)
{
    // 直列に回す。SetNodePoses は共有の Model インスタンスを書き換えるので、
    // 並列にするなら先に Model のポーズの書き込み先を Entity ごとに分けること
    ForEachWithID([this](CCL::ECS::EntityID id, AnimatorComponent &animator, ModelComponent &modelComp) {
        if (!animator.currentSequence) return;

        // エディタで操作中は AnimationSystem::UpdateManual がポーズを作るので触らない
        if (animator.isEditorOverride) return;

        if (Model *model = modelComp.GetModel())
            ComputeAndApplyPose(id, animator, model, animator.currentSequence);
    });
}

void AnimationPoseSystem::ComputeAndApplyPose(CCL::ECS::EntityID id, AnimatorComponent &animator,
                                              Model *model, const AnimSequence *seq)
{
    ZoneScopedN("AnimationPoseSystem::ComputeAndApplyPose");

    int animIndex = model->GetAnimationIndex(seq->targetAnimName.c_str());
    if (animIndex == -1) return;

    // 名前ではなく、実際に移動量を持つノードをルートボーンとして特定する
    const int rootNodeIndex = model->FindRealRootMotionNodeIndex(animIndex);

    if (animator.isBlending && animator.previousSequence)
    {
        ZoneScopedN("AnimationPoseSystem::ComputeAndApplyPose::Blend");

        int prevAnimIndex = model->GetAnimationIndex(animator.previousSequence->targetAnimName.c_str());
        if (prevAnimIndex != -1)
        {
            // バッファのサイズを揃える
            size_t nodeCount = animator.poseBufferA.size();
            if (animator.poseBufferB.size() != nodeCount)
                animator.poseBufferB.resize(nodeCount);

            // 前のクリップと現在のクリップのポーズを計算する
            model->ComputeAnimation(prevAnimIndex, animator.previousTimer, animator.poseBufferB);
            model->ComputeAnimation(animIndex, animator.currentTimer, animator.poseBufferA);

            // Force Root Lock（ブレンド中は前と現在の両方を固定する）。
            //   ノード [0] を固定すると、ルートボーンが 0 番以外（通常は Hips=1）のリグでは
            //   ブレンド中だけクリップの前進が漏れ、ブレンド終了でその場へ戻ってしまう。
            //   前のクリップはルートボーンの番号が違うことがあるので、個別に特定して固定する
            const int prevRootNodeIndex = model->FindRealRootMotionNodeIndex(prevAnimIndex);
            PinRootMotionNode(model, prevAnimIndex, prevRootNodeIndex, animator.poseBufferB, animator.lockVerticalRootMotion);
            PinRootMotionNode(model, animIndex, rootNodeIndex, animator.poseBufferA, animator.lockVerticalRootMotion);

            float blendRate = (animator.blendDuration > 0.0f) ? (animator.currentBlendTime / animator.blendDuration) : 1.0f;
            blendRate       = std::clamp(blendRate, 0.0f, 1.0f);

            // 固定済みのポーズ同士をブレンドする
            Model::BlendAnimations(animator.poseBufferB, animator.poseBufferA, blendRate, animator.poseBufferA);
        }
        else
        {
            model->ComputeAnimation(animIndex, animator.currentTimer, animator.poseBufferA);
        }
    }
    else
    {
        ZoneScopedN("AnimationPoseSystem::ComputeAndApplyPose::Model->ComputeAnimation");

        model->ComputeAnimation(animIndex, animator.currentTimer, animator.poseBufferA);
    }

    // Force Root Lock。RootMotionComponent を持つキャラは位置を常に Transform 側で決めるので、
    // enabled に関係なく必ず固定する。
    //   enabled : 抽出した差分で Transform を動かす → 固定しないと二重に前進する
    //   disabled: コード（ダッシュ等）が Transform を動かす → 固定しないとクリップの前進でモデルだけ滑る
    auto *rootMotion = m_world->GetComponent<RootMotionComponent>(id);
    if (rootMotion && !animator.poseBufferA.empty())
        PinRootMotionNode(model, animIndex, rootNodeIndex, animator.poseBufferA, animator.lockVerticalRootMotion);

    // 確定したポーズを Model に書く（SetNodePoses はここで 1 回だけ）
    {
        ZoneScopedN("AnimationPoseSystem::SetNodePoses");
        model->SetNodePoses(animator.poseBufferA);
    }
}

// ModelPoseUpdateSystem（R03_Animation）がこのポーズを行列へ変換するので、それより前のステージに置く
REGISTER_RENDER_SYSTEM(AnimationPoseSystem, Priority::RenderStage::R02b_SkeletalPose);
