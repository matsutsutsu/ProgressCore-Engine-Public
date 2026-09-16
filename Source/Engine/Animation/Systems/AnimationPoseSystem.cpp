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

// ===================================================================================
// ファイル: AnimationPoseSystem.cpp
// 概要: 骨格ポーズの確定と Model への適用（元 AnimationSystem::ComputeAndApplyPose）。
//       分離の理由はヘッダのコメントを参照。
// ===================================================================================

using CCL::Anim::PinRootMotionNode;

AnimationPoseSystem::AnimationPoseSystem()
    : IfSystem("AnimationPoseSystem")
{
}

void AnimationPoseSystem::Update(float dt)
{
    // 直列実行。Model は共有インスタンスを書き換える(SetNodePoses)ため、
    // 安易な並列化はポーズの競合を招く。並列化する場合は Model のポーズ書き込みを
    // エンティティ単位に分離してから行うこと。
    ForEachWithID([this](CCL::ECS::EntityID id, AnimatorComponent &animator, ModelComponent &modelComp) {
        // 台本（Sequence）がセットされていなければ何もしない
        if (!animator.currentSequence) return;

        // エディタで操作中の場合は AnimationSystem::UpdateManual が姿勢を作るので触らない
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

    // 数学的アプローチによる真のルートボーン特定
    const int rootNodeIndex = model->FindRealRootMotionNodeIndex(animIndex);

    if (animator.isBlending && animator.previousSequence)
    {
        ZoneScopedN("AnimationPoseSystem::ComputeAndApplyPose::Blend");

        int prevAnimIndex = model->GetAnimationIndex(animator.previousSequence->targetAnimName.c_str());
        if (prevAnimIndex != -1)
        {
            // バッファサイズの同期
            size_t nodeCount = animator.poseBufferA.size();
            if (animator.poseBufferB.size() != nodeCount)
                animator.poseBufferB.resize(nodeCount);

            // 過去と現在のポーズを計算
            model->ComputeAnimation(prevAnimIndex, animator.previousTimer, animator.poseBufferB);
            model->ComputeAnimation(animIndex, animator.currentTimer, animator.poseBufferA);

            // Force Root Lock (ブレンド中の過去・現在両方のピン留め)
            //   非ブレンド経路と同じく「真のルートボーン」のXZだけを0秒姿勢へ固定する。
            //   [0]固定にすると、rootNodeIndex が0番以外(通常Hips=1)のリグでは遷移
            //   ブレンド中だけ焼き込み前進が漏れ、ブレンド終了でその場へ戻る「前に出て
            //   すぐ戻る」不具合になる。過去バッファは別クリップ(=別のルートボーン番号)
            //   なので個別に特定してピン留めする。
            const int prevRootNodeIndex = model->FindRealRootMotionNodeIndex(prevAnimIndex);
            PinRootMotionNode(model, prevAnimIndex, prevRootNodeIndex, animator.poseBufferB, animator.lockVerticalRootMotion);
            PinRootMotionNode(model, animIndex, rootNodeIndex, animator.poseBufferA, animator.lockVerticalRootMotion);

            float blendRate = (animator.blendDuration > 0.0f) ? (animator.currentBlendTime / animator.blendDuration) : 1.0f;
            blendRate       = std::clamp(blendRate, 0.0f, 1.0f);

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
        ZoneScopedN("AnimationPoseSystem::ComputeAndApplyPose::Model->ComputeAnimation");

        // ブレンドしていない通常再生
        model->ComputeAnimation(animIndex, animator.currentTimer, animator.poseBufferA);
    }

    // Force Root Lock (真のルートボーンのピン留め)
    //   ルートモーション対応キャラ(RootMotionComponent 保持)は、
    //   「位置の権威は常に Transform 側」なので enabled に関係なく必ずピン留めする。
    //     ・enabled  : 抽出した差分が Transform を動かす → ピンしないと二重に前進する
    //     ・disabled : プログラム(ダッシュ等)が Transform を動かす → ピンしないと
    //                  クリップに焼き込まれた前進でモデルだけが滑っていく
    //   ※ enabled のときだけピン留めしてはいけない。ブレンド中(上の分岐では無条件に
    //     ピン留め)は正常なのに、ブレンド終了と同時にダッシュのモデルが滑り出す。
    auto *rootMotion = m_world->GetComponent<RootMotionComponent>(id);
    if (rootMotion && !animator.poseBufferA.empty())
        PinRootMotionNode(model, animIndex, rootNodeIndex, animator.poseBufferA, animator.lockVerticalRootMotion);

    // 最終確定したポーズを行列へ反映（SetNodePosesはここに1回だけ！）
    {
        ZoneScopedN("AnimationPoseSystem::SetNodePoses");
        model->SetNodePoses(animator.poseBufferA);
    }
}

// ModelPoseUpdateSystem(R03_Animation) より必ず前のステージへ置く。
//   ポーズ(node.position/rotation/scale)を確定させてから、あちらが行列へ変換する。
REGISTER_RENDER_SYSTEM(AnimationPoseSystem, Priority::RenderStage::R02b_SkeletalPose);
