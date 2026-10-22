#include "TrailAnimationSyncSystem.h"

#include "Engine/Animation/Components/AnimatorComponent.h"
#include "Engine/Animation/Core/AnimSequence.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

using namespace CCL::ECS;

void TrailAnimationSyncSystem::Update(float /*dt*/)
{
    ForEach([this](const TransformComponent &transform, TrailComponent &trail) {
        // 処理フロー:
        // 1. owner（Animator を持つ祖先）を確かめる
        // 2. animControlled なら、owner の再生時刻から emitting を決める

        // ---------------------------------------------------------
        // 1. owner の確認
        // ---------------------------------------------------------

        // 持っている owner が消えていたら解く。
        // 消えた Entity の ID を持ち続けると、ID が使い回されたときに別の Entity を owner と見なすため。
        const AnimatorComponent *animator = nullptr;
        if (trail.ownerEntity != InvalidEntityID)
        {
            animator = m_world->GetComponent<AnimatorComponent>(trail.ownerEntity);
            if (!animator)
            {
                trail.ownerEntity = InvalidEntityID;
            }
        }

        // 見つかったら以後は親をたどらない。
        // 見つからない間は毎フレーム探し直す（階層が後から組まれる場合に追従するため）。
        if (trail.ownerEntity == InvalidEntityID)
        {
            trail.ownerEntity = FindAnimatorAncestor(transform);
            if (trail.ownerEntity != InvalidEntityID)
            {
                animator = m_world->GetComponent<AnimatorComponent>(trail.ownerEntity);
            }
        }

        // ---------------------------------------------------------
        // 2. emitting の決定
        // ---------------------------------------------------------

        // 手動のトレイルには触れない（emitting はインスペクタや ActivateFor の値のまま）
        if (!trail.animControlled)
        {
            return;
        }

        // 再生中のアニメーションが無いので出さない
        if (!animator || !animator->currentSequence)
        {
            trail.emitting = false;
            return;
        }

        trail.emitting = IsInsideTrailEvent(*animator);
    });
}

EntityID TrailAnimationSyncSystem::FindAnimatorAncestor(const TransformComponent &transform) const
{
    EntityID cur   = transform.parent; // EntityLink → EntityID 暗黙変換
    int      guard = 0;

    while (cur != InvalidEntityID && cur != 0 && guard++ < 32)
    {
        if (m_world->GetComponent<AnimatorComponent>(cur))
        {
            return cur;
        }

        const auto *parentTransform = m_world->GetComponent<TransformComponent>(cur);
        cur                         = parentTransform ? static_cast<EntityID>(parentTransform->parent) : InvalidEntityID;
    }

    return InvalidEntityID;
}

bool TrailAnimationSyncSystem::IsInsideTrailEvent(const AnimatorComponent &animator) const
{
    // タグは見ず、どれか1つの Trail 区間に入っていればオンにする。
    // 二刀流などで出し分けるなら、ev.tagHash との照合を足す。
    for (const auto &ev : animator.currentSequence->events)
    {
        if (ev.type != AnimEventType::Trail)
        {
            continue;
        }

        if (animator.currentTimer >= ev.startTime && animator.currentTimer <= ev.endTime)
        {
            return true;
        }
    }

    return false;
}

// AnimationSystem(L02_PostUpdate) の後、TrailRecordSystem(L06) と描画の TrailSystem(R08) の前に走る。
REGISTER_LOGIC_SYSTEM(TrailAnimationSyncSystem, Priority::LogicStage::L05_Collision);
