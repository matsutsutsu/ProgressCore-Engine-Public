#pragma once
#include "ECS/Core/CCL_World.h"
#include "Game/Logics/Combat/Weapon/Attachments/AttachmentComponents.h"
#include "Game/Logics/Combat/Weapon/Core/BulletContextComponent.h"
#include <DirectXMath.h>

namespace BulletAttachmentLogic {
    // 爆発Entityを生成する
    void SpawnExplosion(CCL::ECS::Core::World *world,
        const DirectX::XMFLOAT3               &position,
        const ExplosionData                   &data,
        float                                  baseDamage,
        CCL::ECS::EntityID                     parentID);

    // 子弾を生成する（分裂用）
    void SpawnChildBullet(CCL::ECS::Core::World *world,
        const DirectX::XMFLOAT3                 &position,
        const DirectX::XMFLOAT3                 &parentVelocity,
        const SplitData                         &data,
        float                                    baseDamage,
        const BulletContextComponent            &parentCtx);
} // namespace BulletAttachmentLogic