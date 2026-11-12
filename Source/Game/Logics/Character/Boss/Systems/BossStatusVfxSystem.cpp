#include "BossStatusVfxSystem.h"

#include "Game/Logics/Character/Boss/Components/BossActionComponent.h"
#include "Game/Logics/Character/Boss/Components/BossCommandComponent.h"
#include "Engine/ECS/Common/CCL_TypeInfo.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

using namespace CCL::ECS;



std::vector<CCL::ECS::TypeID> BossStatusVfxSystem::GetReadTypes() const
{
    auto types = Base::GetReadTypes();
    types.push_back(TypeInfo<BossCommandComponent>::ID()); // 怒り中か（ボスは別のエンティティ）
    types.push_back(TypeInfo<BossActionComponent>::ID());  // 疲れているか
    return types;
}



void BossStatusVfxSystem::Update(float /*dt*/)
{
    // 処理フロー:
    // 1. owner（BossCommandComponent を持つ祖先）を探して持つ
    // 2. ボスがその状態の間だけ出し、入った瞬間にバーストを起こす

    ForEach([this](const TransformComponent &transform, BossStatusVfxComponent &vfx, GPUParticleComponent &particle) {
        // ---------------------------------------------------------
        // 1. owner を探す
        // ---------------------------------------------------------

        // 見つかったら以後は親をたどらない。プレハブの親子が組まれる前のフレームもあるので、見つかるまで毎フレーム探す
        // 親の循環に備えて 32 段で打ち切る
        if (vfx.owner == InvalidEntityID)
        {
            EntityID cur   = transform.parent;
            int      guard = 0;
            while (cur != InvalidEntityID && cur != 0 && guard++ < 32)
            {
                if (m_world->GetComponent<BossCommandComponent>(cur)) break;
                const auto *pt = m_world->GetComponent<TransformComponent>(cur);
                cur            = pt ? static_cast<EntityID>(pt->parent) : InvalidEntityID;
            }
            vfx.owner = (cur != 0) ? cur : InvalidEntityID;
        }

        // ---------------------------------------------------------
        // 2. 出し入れ
        // ---------------------------------------------------------
        const bool active = (vfx.owner != InvalidEntityID) && IsInStatus(vfx.owner, vfx.kind);

        // 入った瞬間に一気に広げる（バーストのエミッタだけが反応する）。始まりを動きの最中でも気付かせるため
        if (active && !vfx.wasActive) particle.TriggerBurst();

        // 止めても出ている粒子は寿命まで残るので、状態の終わりで尾を引いて消える
        particle.SetEmitting(active);
        vfx.wasActive = active;
    });
}



bool BossStatusVfxSystem::IsInStatus(EntityID boss, BossStatusVfxKind kind) const
{
    switch (kind)
    {
    case BossStatusVfxKind::Rage: {
        const auto *cmd = m_world->GetComponent<BossCommandComponent>(boss);
        return cmd && cmd->raging;
    }
    case BossStatusVfxKind::Exhausted: {
        const auto *act = m_world->GetComponent<BossActionComponent>(boss);
        return act && act->currentState == BossActionState::Exhausted;
    }
    }
    return false;
}



// ボスの状態が決まる L02 の後。ほかに GPUParticleComponent を書くロジックの System がない段に置く
REGISTER_LOGIC_SYSTEM(BossStatusVfxSystem, Priority::LogicStage::L06_HudSync);
