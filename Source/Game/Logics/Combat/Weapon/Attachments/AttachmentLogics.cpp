#include "AttachmentLogics.h"
#include "Engine/GamePlay/Physics/Collision/SphereColliderComponent.h"
#include "Engine/GamePlay/Physics/RigidBody/RigidBodyComponent.h"
#include "Engine/GamePlay/Transform/TransformComponent.h"
#include "Engine/GamePlay/Utils/TimerComponent.h"
#include "Engine/Serialization/Factory/Prefab.h"
#include "Game/Logics/Combat/Weapon/Attachments/BulletHitStateComponent.h"
#include "Game/Logics/Combat/Weapon/Core/GunBuilder.h"


namespace BulletAttachmentLogic {

    void SpawnExplosion(CCL::ECS::Core::World *world,
        const DirectX::XMFLOAT3               &position,
        const ExplosionData                   &data,
        float                                  baseDamage,
        CCL::ECS::EntityID                     parentID)
    {
        // ※旧 BulletEffectLibrary.cpp の Explosion::SpawnExplosion の中身をここに移植
        auto               arch = CCL::ECS::ArchetypeHelper::Generate<TransformComponent,
                          RigidBodyComponent,
                          SphereColliderComponent,
                          BulletContextComponent,
                          TimerComponent,
                          BulletHitStateComponent>();
        CCL::ECS::EntityID id   = world->RequestSpawnEntity(arch);

        TransformComponent trans;
        trans.position = position;
        trans.rotation = DirectX::XMFLOAT4(0, 0, 0, 1);
        world->AddComponent<TransformComponent>(id, trans);

        RigidBodyComponent rb;
        rb.velocity           = DirectX::XMFLOAT3(0, 0, 0);
        rb.useGravity         = false;
        rb.ignoreMapCollision = true;
        world->AddComponent<RigidBodyComponent>(id, rb);

        SphereColliderComponent col;
        col.radius = data.radius;
        col.layer  = Layer::Bullet;
        col.mask   = Layer::Enemy;
        world->AddComponent<SphereColliderComponent>(id, col);

        BulletContextComponent ctx;
        ctx.currentDamage = baseDamage * data.damageMult;
        ctx.lifeTime      = 0.3f;
        ctx.pierceCount   = 999;
        ctx.selfID        = id;
        ctx.blueprint     = nullptr;
        ctx.hitHistory.Add(parentID); // 親(自分自身)には当たらないようにする
        world->AddComponent<BulletContextComponent>(id, ctx);

        // 忘れずにコンポーネントを初期化して追加する
        BulletHitStateComponent hitState;
        hitState.Reset();
        world->AddComponent<BulletHitStateComponent>(id, hitState);



        Prefab::SpawnParticle(*world,
            "Assets/GamePlay/Prefabs/Particles/VFX_Explosion_Base.json",
            id,
            DirectX::XMFLOAT3(0, 0, 0));
    }




    void SpawnChildBullet(CCL::ECS::Core::World *world,
        const DirectX::XMFLOAT3                 &position,
        const DirectX::XMFLOAT3                 &parentVelocity,
        const SplitData                         &data,
        float                                    baseDamage,
        const BulletContextComponent            &parentCtx)
    {
        // 臨時GunBuilderを作成し、親のBlueprintからアタッチメント構成を引き継ぐ
        GunBuilder gb;
        gb.baseDamage      = baseDamage;
        gb.baseBulletCount = data.count;       // 分裂数
        gb.baseSpread      = data.angleSpread; // 拡散角
        gb.baseSpeed =
            DirectX::XMVectorGetX(DirectX::XMVector3Length(DirectX::XMLoadFloat3(&parentVelocity)));

        // 親のアタッチメントをコピー（Split自身など、引き継ぎたくないものは除外する制御が必要であればここに書く）
        const BulletBlueprint *bp = parentCtx.blueprint;
        for (int i = 1; i <= (int)EffectID::Reflection; ++i) {
            EffectID effect = static_cast<EffectID>(i);
            if (effect == EffectID::Split) continue; // 無限分裂を防ぐためSplitは引き継がない

            // Travelのレベルを取得し、0より大きければ1つだけ構造体として追加する
            int travelLevel = bp->GetEffectCount(BulletPhase::Travel, effect);
            if (travelLevel > 0) {
                gb.travelMods.push_back({effect, travelLevel});
            }

            // OnHitのレベルを取得し、0より大きければ1つだけ構造体として追加する
            int hitLevel = bp->GetEffectCount(BulletPhase::OnHit, effect);
            if (hitLevel > 0) {
                gb.onHitMods.push_back({effect, hitLevel});
            }
        }

        DirectX::XMFLOAT3 dir = {0, 0, 1};
        if (gb.baseSpeed > 0.001f) {
            DirectX::XMStoreFloat3(
                &dir, DirectX::XMVector3Normalize(DirectX::XMLoadFloat3(&parentVelocity)));
        }

        // 子弾を発射
        gb.Fire(world, position, dir);
    }

} // namespace BulletAttachmentLogic