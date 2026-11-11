#include "ParticleAnimationSyncSystem.h"

#include "Engine/Animation/Components/AnimatorComponent.h"
#include "Engine/Animation/Core/AnimSequence.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

using namespace CCL::ECS;

void ParticleAnimationSyncSystem::Update(float /*dt*/)
{
    // 処理フロー:
    // 1. owner（Animator を持つ祖先）を探して持つ
    // 2. owner の Animator を読む
    // 3. 今の再生時刻が Particle 区間に入っているかで emitting を決める

    ForEachWithID([this](EntityID /*id*/, const TransformComponent &transform, GPUParticleComponent &particle) {
        // 手動のエミッタには触れない（emitting はインスペクタの値のまま）
        if (!particle.animControlled)
            return;

        // ---------------------------------------------------------
        // 1. owner を探す
        // ---------------------------------------------------------

        // 見つかったら以後は親をたどらない。見つからない間は毎フレーム探し直す（階層が後から組まれる場合）
        //   親の循環に備えて 32 段で打ち切る
        if (particle.ownerEntity == InvalidEntityID)
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
            particle.ownerEntity = (cur != 0) ? cur : InvalidEntityID;
        }

        // ---------------------------------------------------------
        // 2. owner の Animator（別の Entity なので読むだけ）
        // ---------------------------------------------------------
        const AnimatorComponent *animator =
            (particle.ownerEntity != InvalidEntityID)
                ? m_world->GetComponent<AnimatorComponent>(particle.ownerEntity)
                : nullptr;

        if (!animator || !animator->currentSequence)
        {
            particle.SetEmitting(false); // 再生中のアニメーションがないので出さない
            return;
        }

        // ---------------------------------------------------------
        // 3. Particle 区間の判定
        // ---------------------------------------------------------

        // タグは見ず、どれか 1 つの Particle 区間に入っていればオンにする
        //   止めても出ている粒子は寿命まで残るので、区間の終わりで尾を引いて消える
        const AnimSequence *seq    = animator->currentSequence;
        bool                inside = false;
        for (const auto &ev : seq->events)
        {
            if (ev.type != AnimEventType::Particle)
                continue;
            if (animator->currentTimer >= ev.startTime &&
                animator->currentTimer <= ev.endTime)
            {
                inside = true;
                break;
            }
        }

        // 全エミッタへまとめて反映する
        //   タグで出し分けるなら、tagHash は HitBox にしか求めていないので、
        //   ResourceManager::LoadAnimSequence で Particle にも求めるところから広げる
        particle.SetEmitting(inside);
    });
}



// AnimationSystem（L02_PostUpdate）の後、描画の GPUParticleUpdateSystem（R07）の前に走る
REGISTER_LOGIC_SYSTEM(ParticleAnimationSyncSystem, Priority::LogicStage::L05_Collision);
