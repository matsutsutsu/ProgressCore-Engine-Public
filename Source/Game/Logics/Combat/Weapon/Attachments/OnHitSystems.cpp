#include "OnHitSystems.h"
#include "Game/Logics/Combat/Weapon/Attachments/AttachmentLogics.h"
#include <random>
#include <SimpleMath.h>

// システムの実行順序の定義ヘッダー
#include "ECS/System/CCL_SystemRegistry.h"
#include "Game/Core/SystemPriority.h"

using namespace DirectX::SimpleMath;

static float GetRandomFloat(float min, float max)
{
    static std::random_device             rd;
    static std::mt19937                   gen(rd());
    std::uniform_real_distribution<float> dis(min, max);
    return dis(gen);
}

void OnHitExplosionSystem::Update(float dt)
{
    ForEach([&](const TransformComponent &trans,
                const BulletContextComponent &ctx,
                const BulletHitStateComponent &hitState,
                const OnHitExplosionComponent &exp) {
        if (!hitState.isHitThisFrame) return;
        BulletAttachmentLogic::SpawnExplosion(
            _world, hitState.hitPosition, exp.data, ctx.currentDamage, ctx.selfID);
    });
}

void OnHitSplitSystem::Update(float dt)
{
    ForEach([&](const RigidBodyComponent &rb,
                const TransformComponent &trans,
                const BulletContextComponent &ctx,
                BulletHitStateComponent &hitState,
                const OnHitSplitComponent     &split) {
        if (!hitState.isHitThisFrame) return;

        // 子弾を生成する
        BulletAttachmentLogic::SpawnChildBullet(
            _world, hitState.hitPosition, rb.velocity, split.data, ctx.currentDamage, ctx);

        // 分裂はエネルギーを使い果たすため、貫通力が残っていても強制的に死亡させる
        hitState.isFatal = true;
    });
}

void OnHitReflectionSystem::Update(float dt)
{
    ForEach(
        [&](RigidBodyComponent &rb, BulletHitStateComponent &hitState, OnHitReflectComponent &ref) {
            if (!hitState.isHitThisFrame) return;

            // 壁（targetID == Invalid）に当たった時のみ反射を許可
            if (hitState.targetID != CCL::ECS::InvalidEntityID) return;

            if (ref.remainingCount > 0) {
                Vector3 vel    = rb.velocity;
                Vector3 normal = hitState.hitNormal;

                // 反射ベクトルの計算: R = V - 2(V・N)N
                Vector3 reflected = Vector3::Reflect(vel, normal);
                rb.velocity       = reflected;

                ref.remainingCount--;
                hitState.isFatal = false; // 反射したので死なない
            }
        });
}

void OnHitChaosSystem::Update(float dt)
{
    ForEach([&](RigidBodyComponent &rb,
                const BulletHitStateComponent &hitState,
                const OnHitChaosComponent     &chaos) {
            if (!hitState.isHitThisFrame) return;

            // 当たった瞬間にランダムな方向に弾け飛ぶ（貫通時など）
            float   randomAngle = GetRandomFloat(-90.0f, 90.0f);
            Vector3 vel         = rb.velocity;
            Matrix  rot         = Matrix::CreateRotationY(DirectX::XMConvertToRadians(randomAngle));
            rb.velocity         = Vector3::TransformNormal(vel, rot) * chaos.data.speedMultRange;
        });
}

void OnHitAccelerateSystem::Update(float dt)
{
    ForEach([&](RigidBodyComponent       &rb,
                const BulletHitStateComponent &hitState,
                const OnHitAccelerateComponent &accel) {
        if (!hitState.isHitThisFrame) return;
        // 貫通した際に加速する
        rb.velocity = rb.velocity * (1.0f + accel.data.rate);
    });
}



// 弾のOnHit (着弾時の挙動)
REGISTER_LOGIC_SYSTEM(OnHitExplosionSystem, Priority::LogicStage::Resolution);
REGISTER_LOGIC_SYSTEM(OnHitSplitSystem, Priority::LogicStage::Resolution);
REGISTER_LOGIC_SYSTEM(OnHitReflectionSystem, Priority::LogicStage::Resolution);
REGISTER_LOGIC_SYSTEM(OnHitChaosSystem, Priority::LogicStage::Resolution);
REGISTER_LOGIC_SYSTEM(OnHitAccelerateSystem, Priority::LogicStage::Resolution);