#include "TrailAnimationSyncSystem.h"

#include "Engine/Animation/Components/AnimatorComponent.h"
#include "Engine/Animation/Core/AnimOwnerUtils.h"
#include "Engine/Animation/Core/AnimSequence.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

using namespace CCL::ECS;



void TrailAnimationSyncSystem::Update(float dt)
{
    ForEach([this](const TransformComponent &transform, TrailComponent &trail) {
        // 持ち主（Animator を持つ祖先）を確かめ、消えていたら親をたどって探し直す
        trail.ownerEntity = CCL::Anim::RefreshAnimatorOwner(*m_world, transform, trail.ownerEntity);

        // 手動のトレイルには触れない（emitting はインスペクタや ActivateFor の値のまま）
        if (!trail.animControlled) return;

        // 再生中のアニメーションがないので出さない
        const AnimatorComponent *animator = m_world->GetComponent<AnimatorComponent>(trail.ownerEntity);
        if (!animator || !animator->currentSequence)
        {
            trail.emitting = false;
            return;
        }

        // どれか 1 つの Trail 区間に入っていれば、持ち主の子にあるトレイルをすべてオンにする
        trail.emitting = IsInEventWindow(*animator->currentSequence, AnimEventType::Trail, animator->currentTimer);
    });
}



// AnimationSystem（L02_PostUpdate）の後、TrailRecordSystem（L06）と描画の TrailSystem（R08）の前に走る
REGISTER_LOGIC_SYSTEM(TrailAnimationSyncSystem, Priority::LogicStage::L05_Collision);
