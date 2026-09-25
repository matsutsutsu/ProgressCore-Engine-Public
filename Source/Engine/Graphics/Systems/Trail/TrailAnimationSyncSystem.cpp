#include "TrailAnimationSyncSystem.h"

#include "Engine/Animation/Components/AnimatorComponent.h"
#include "Engine/Animation/Core/AnimSequence.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

using namespace CCL::ECS;

void TrailAnimationSyncSystem::Update(float /*dt*/)
{
    ForEachWithID([this](EntityID /*id*/, const TransformComponent &transform, TrailComponent &trail) {
        // 手動のトレイルには触れない（emitting はインスペクタの値のまま）
        if (!trail.animControlled)
            return;

        // ── 1. owner（Animator を持つ祖先）を探して持つ ──
        //   見つかったら以後は親をたどらない。見つからない間は毎フレーム探し直す
        //   （階層が後から組まれる場合に追従する）。親の循環に備えて 32 段で打ち切る。
        if (trail.ownerEntity == InvalidEntityID)
        {
            EntityID cur   = transform.parent; // EntityLink → EntityID 暗黙変換
            int      guard = 0;
            while (cur != InvalidEntityID && cur != 0 && guard++ < 32)
            {
                if (m_world->GetComponent<AnimatorComponent>(cur))
                    break;
                const auto *pt = m_world->GetComponent<TransformComponent>(cur);
                cur            = pt ? static_cast<EntityID>(pt->parent) : InvalidEntityID;
            }
            trail.ownerEntity = (cur != 0) ? cur : InvalidEntityID;
        }

        // ── 2. owner の Animator を読む（別の Entity なので読むだけ） ──
        const AnimatorComponent *animator =
            (trail.ownerEntity != InvalidEntityID)
                ? m_world->GetComponent<AnimatorComponent>(trail.ownerEntity)
                : nullptr;

        if (!animator || !animator->currentSequence)
        {
            trail.emitting = false; // 再生中のアニメーションが無いので出さない
            return;
        }

        // ── 3. 今の再生時刻が Trail 区間に入っているかで emitting を決める ──
        //   タグは見ず、どれか1つの Trail 区間に入っていればオンにする。
        //   二刀流などで出し分けるなら、ev.tagHash との照合を足す。
        const AnimSequence *seq    = animator->currentSequence;
        bool                inside = false;
        for (const auto &ev : seq->events)
        {
            if (ev.type != AnimEventType::Trail)
                continue;
            if (animator->currentTimer >= ev.startTime &&
                animator->currentTimer <= ev.endTime)
            {
                inside = true;
                break;
            }
        }
        trail.emitting = inside;
    });
}

// AnimationSystem(L02_PostUpdate) の後、描画の TrailSystem(R08) の前に走る。
REGISTER_LOGIC_SYSTEM(TrailAnimationSyncSystem, Priority::LogicStage::L05_Collision);
