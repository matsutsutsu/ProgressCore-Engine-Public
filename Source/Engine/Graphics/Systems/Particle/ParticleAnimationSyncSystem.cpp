#include "ParticleAnimationSyncSystem.h"

#include "Engine/Animation/Components/AnimatorComponent.h"
#include "Engine/Animation/Core/AnimOwnerUtils.h"
#include "Engine/Animation/Core/AnimSequence.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

using namespace CCL::ECS;

void ParticleAnimationSyncSystem::Update(float dt)
{
    ForEach([this](const TransformComponent &transform, GPUParticleComponent &particle) {
        // 手動のエミッタには触れない（emitting はインスペクタの値のまま）
        if (!particle.animControlled) return;

        // 持ち主（Animator を持つ祖先）を確かめ、消えていたら親をたどって探し直す
        particle.ownerEntity = CCL::Anim::RefreshAnimatorOwner(*m_world, transform, particle.ownerEntity);

        // 再生中のアニメーションがないので出さない
        const AnimatorComponent *animator = m_world->GetComponent<AnimatorComponent>(particle.ownerEntity);
        if (!animator || !animator->currentSequence)
        {
            particle.SetEmitting(false);
            return;
        }

        // どれか 1 つの Particle 区間に入っていれば、持ち主の子にあるエミッタをすべてオンにする
        //   止めても出ている粒子は寿命まで残るので、区間の終わりで尾を引いて消える
        particle.SetEmitting(IsInEventWindow(*animator->currentSequence, AnimEventType::Particle, animator->currentTimer));
    });
}



// AnimationSystem（L02_PostUpdate）の後、描画の GPUParticleUpdateSystem（R07）の前に走る
REGISTER_LOGIC_SYSTEM(ParticleAnimationSyncSystem, Priority::LogicStage::L05_Collision);
