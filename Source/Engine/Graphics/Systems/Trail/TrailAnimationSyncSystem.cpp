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
        // アニメ制御でなくても探す。TrailRecordSystem が一時トレイルを持ち主の時計で進めるため
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

        // 今の再生時刻が、どれか1つの Trail 区間に入っていれば出す
        // 同じ持ち主の下にあるトレイルは、すべて同時にオンになる
        trail.emitting = IsInEventWindow(*animator->currentSequence, AnimEventType::Trail, animator->currentTimer);
    });
}



REGISTER_LOGIC_SYSTEM(TrailAnimationSyncSystem, Priority::LogicStage::L05_Collision);
