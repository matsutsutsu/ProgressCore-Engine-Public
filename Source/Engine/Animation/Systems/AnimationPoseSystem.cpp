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

AnimationPoseSystem::AnimationPoseSystem() : IfSystem("AnimationPoseSystem") {}



void AnimationPoseSystem::Update(float dt)
{
    // 直列に回す。Model は Entity ごとのインスタンスだが、
    //   並列にするなら、ポーズ計算の中で書く Model のキャッシュ（mutable）と
    //   m_world->GetComponent を並列に呼んで安全かを先に確かめること
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
    // 処理フロー:
    // 1. クリップの番号とルートボーンを引く
    // 2. ポーズを計算する（今側・前側それぞれ、単体のクリップかブレンドスペース。ブレンド中は前後を混ぜる）
    // 3. Force Root Lock を掛ける
    // 4. Model に書く

    ZoneScopedN("AnimationPoseSystem::ComputeAndApplyPose");

    // ---------------------------------------------------------
    // 1. クリップとルートボーン
    // ---------------------------------------------------------

    // ブレンドスペースのときは、時間の基準（点 [0]）のクリップ
    int animIndex = model->GetAnimationIndex(seq->targetAnimName.c_str());
    if (animIndex == -1) return;

    // 名前ではなく、実際に移動量を持つノードをルートボーンとして特定する
    const int rootNodeIndex = model->FindRealRootMotionNodeIndex(animIndex);

    // ---------------------------------------------------------
    // 2. ポーズの計算
    // ---------------------------------------------------------

    // 前側を作れたときだけクロスフェードする（前のクリップがモデルになければ、今側だけを出す）
    const bool crossFading = animator.isBlending && animator.previousSequence;
    if (crossFading && ComputeSourcePose(model, animator, true, true))
    {
        ZoneScopedN("AnimationPoseSystem::ComputeAndApplyPose::Blend");

        // ブレンド中は前と現在の両方を、混ぜる前に固定する
        //   前のクリップはルートボーンの番号が違うことがあるので、ComputeSourcePose が個別に特定する
        //   ノード [0] 決め打ちだと、Hips=1 のリグでブレンド中だけ前進が漏れ、終了でその場へ戻る
        ComputeSourcePose(model, animator, false, true);

        float blendRate = (animator.blendDuration > 0.0f) ? (animator.currentBlendTime / animator.blendDuration) : 1.0f;
        blendRate       = std::clamp(blendRate, 0.0f, 1.0f);

        Model::BlendAnimations(animator.poseBufferB, animator.poseBufferA, blendRate, animator.poseBufferA);
    }
    else
    {
        ZoneScopedN("AnimationPoseSystem::ComputeAndApplyPose::Model->ComputeAnimation");

        // 単体のクリップは 3. でまとめて固定する
        ComputeSourcePose(model, animator, false, false);
    }

    // ---------------------------------------------------------
    // 3. Force Root Lock
    // ---------------------------------------------------------

    // RootMotionComponent を持つキャラは位置を常に Transform 側で決めるので、enabled に関係なく固定する
    //   enabled なら抽出した差分で Transform を動かすので、固定しないと二重に前進する
    //   disabled ならコード（ダッシュなど）が Transform を動かすので、固定しないとモデルだけ滑る
    auto *rootMotion = m_world->GetComponent<RootMotionComponent>(id);
    if (rootMotion && !animator.poseBufferA.empty())
        PinRootMotionNode(model, animIndex, rootNodeIndex, animator.poseBufferA, animator.lockVerticalRootMotion);

    // ---------------------------------------------------------
    // 4. Model へ書く
    // ---------------------------------------------------------
    {
        ZoneScopedN("AnimationPoseSystem::SetNodePoses");
        model->SetNodePoses(animator.poseBufferA);
    }
}



bool AnimationPoseSystem::ComputeSourcePose(Model *model, AnimatorComponent &animator, bool previous, bool pinClip)
{
    // 処理フロー:
    // 1. 今側か前側かで、読む値と書く先を選ぶ
    // 2. 単体のクリップなら、そのまま計算する
    // 3. ブレンドスペースなら、重みのある点を計算して混ぜる

    // ---------------------------------------------------------
    // 1. 今側か前側か
    // ---------------------------------------------------------
    const AnimSequence           *timeBase = previous ? animator.previousSequence : animator.currentSequence;
    const float                   time     = previous ? animator.previousTimer : animator.currentTimer;
    const AnimBlendPlayback      &blend    = previous ? animator.previousBlend : animator.currentBlend;
    std::vector<Model::NodePose> &out      = previous ? animator.poseBufferB : animator.poseBufferA;

    const int baseIndex = model->GetAnimationIndex(timeBase->targetAnimName.c_str());
    if (baseIndex == -1) return false;

    // ---------------------------------------------------------
    // 2. 単体のクリップ
    // ---------------------------------------------------------
    if (!blend.IsActive())
    {
        model->ComputeAnimation(baseIndex, time, out);
        if (pinClip)
        {
            PinRootMotionNode(model, baseIndex, model->FindRealRootMotionNodeIndex(baseIndex), out,
                              animator.lockVerticalRootMotion);
        }
        return true;
    }

    // ---------------------------------------------------------
    // 3. ブレンドスペース
    // ---------------------------------------------------------
    ZoneScopedN("AnimationPoseSystem::ComputeSourcePose::BlendSpace");

    // 時間の基準の進み具合（0〜1）を、各点のクリップの長さに掛けて時刻にする
    //   長さの違うクリップでも、同じ進み具合の姿勢を混ぜるので、足の運びがそろう
    const float progress = (timeBase->duration > 0.0f) ? std::clamp(time / timeBase->duration, 0.0f, 1.0f) : 0.0f;

    // 累計の重みで割りながら足し込むと、何番目に混ぜても重み付きの平均になる
    //   回転は球面補間なので厳密な平均ではないが、隣り合う 2〜3 点では差は見えない
    constexpr float kMinSampleWeight = 0.001f; // これより軽い点は見た目に効かないので計算しない
    float           accumulated      = 0.0f;
    for (int i = 0; i < blend.sampleCount; ++i)
    {
        const float         weight = blend.weights[i];
        const AnimSequence *sample = blend.sequences[i];
        if (weight < kMinSampleWeight || !sample) continue;

        const int sampleIndex = model->GetAnimationIndex(sample->targetAnimName.c_str());
        if (sampleIndex == -1) continue;

        // 1 本目は out へ直接、2 本目からは作業用のバッファへ計算して混ぜる
        const bool                    first  = (accumulated == 0.0f);
        std::vector<Model::NodePose> &target = first ? out : animator.poseBufferC;
        model->ComputeAnimation(sampleIndex, progress * sample->duration, target);

        // 混ぜる前に、点ごとにルートを固定する（点ごとにルートボーンの番号と前進量が違う）
        PinRootMotionNode(model, sampleIndex, model->FindRealRootMotionNodeIndex(sampleIndex), target,
                          animator.lockVerticalRootMotion);

        accumulated += weight;
        if (!first)
        {
            Model::BlendAnimations(out, target, weight / accumulated, out);
        }
    }

    // 混ぜられる点が 1 つもない（クリップがモデルにないなど）ときは、時間の基準のクリップだけを出す
    if (accumulated == 0.0f)
    {
        model->ComputeAnimation(baseIndex, time, out);
    }
    return true;
}



// ModelPoseUpdateSystem（R03_Animation）がこのポーズを行列へ変換するので、それより前のステージに置く
REGISTER_RENDER_SYSTEM(AnimationPoseSystem, Priority::RenderStage::R02b_SkeletalPose);
