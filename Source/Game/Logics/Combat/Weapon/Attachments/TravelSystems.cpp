#include "TravelSystems.h"
#include "Game/Logics/Combat/HealthComponent.h"
#include "Game/Logics/Combat/Weapon/Attachments/AttachmentLogics.h"
#include <random>
#include <SimpleMath.h>

// システムの実行順序の定義ヘッダー
#include "ECS/System/CCL_SystemRegistry.h"
#include "Game/Core/SystemPriority.h"

using namespace CCL::ECS;
using namespace DirectX::SimpleMath;

static float GetRandomFloat(float min, float max)
{
    static std::random_device             rd;
    static std::mt19937                   gen(rd());
    std::uniform_real_distribution<float> dis(min, max);
    return dis(gen);
}

void TravelHomingSystem::Update(float dt)
{
    //ForEach([&](RigidBodyComponent &rb, TransformComponent &trans, TravelHomingComponent &homing) {
    //    // ターゲットを探す (※警告：O(N*M)の重い処理。将来的に空間分割必須)
    //    if (homing.data.targetID == CCL::ECS::InvalidEntityID) {
    //        float    closestDistSq = 400.0f; // 探索半径(約20m)
    //        EntityID bestTarget    = InvalidEntityID;

    //        _world->ForEach([&](EntityID id, HealthComponent &hp, TransformComponent &enemyTrans) {
    //            if (hp.isDead) return;
    //            float distSq = Vector3::DistanceSquared(trans.position, enemyTrans.position);
    //            if (distSq < closestDistSq) {
    //                closestDistSq = distSq;
    //                bestTarget    = id;
    //            }
    //        });
    //        homing.data.targetID = bestTarget;
    //    }

    //    // 誘導処理
    //    if (homing.data.targetID != CCL::ECS::InvalidEntityID) {
    //        if (auto *enemyTrans = _world->GetComponent<TransformComponent>(homing.data.targetID)) {
    //            Vector3 currentVel = rb.velocity;
    //            float   speed      = currentVel.Length();
    //            if (speed > 0.001f) {
    //                Vector3 currentDir = currentVel / speed;
    //                Vector3 targetDir  = enemyTrans->position - trans.position;
    //                targetDir.y += 1.0f; // 敵の中心を狙う補正
    //                targetDir.Normalize();

    //                currentDir = Vector3::Lerp(currentDir, targetDir, homing.data.turnSpeed * dt);
    //                currentDir.Normalize();
    //                rb.velocity = currentDir * speed;
    //            }
    //        }
    //        else {
    //            homing.data.targetID = CCL::ECS::InvalidEntityID; // 敵が死んだらリセット
    //        }
    //    }
    //});
}

void TravelAccelerateSystem::Update(float dt)
{
    ForEach([&](RigidBodyComponent &rb, const TravelAccelerateComponent &accel) {
        Vector3 vel   = rb.velocity;
        float   speed = vel.Length();
        if (speed > 0.001f) {
            speed += (speed * accel.data.rate) * dt;
            rb.velocity = (vel / vel.Length()) * speed;
        }
    });
}

void TravelSlowSystem::Update(float dt)
{
    ForEach([&](RigidBodyComponent &rb, const TravelSlowComponent &slow) {
        Vector3 vel   = rb.velocity;
        float   speed = vel.Length();
        if (speed > 0.001f) {
            speed -= (speed * slow.data.brakePower) * dt;
            if (speed < 0.5f) speed = 0.5f; // 停止させない
            rb.velocity = (vel / vel.Length()) * speed;
        }
    });
}

void TravelExplosionSystem::Update(float dt)
{
    ForEach([&](const TransformComponent &trans,
                const BulletContextComponent &ctx,
                TravelExplosionComponent &exp) {
            exp.intervalTimer -= dt;
            if (exp.intervalTimer <= 0.0f) {
                exp.intervalTimer = 0.15f;
                BulletAttachmentLogic::SpawnExplosion(
                    _world, trans.position, exp.data, ctx.currentDamage, ctx.selfID);
            }
        });
}

void TravelSplitSystem::Update(float dt)
{
    ForEach([&](RigidBodyComponent     &rb,
                const TransformComponent &trans,
                const BulletContextComponent &ctx,
                TravelSplitComponent   &split) {
        if (split.hasSplit) return;
        split.intervalTimer -= dt;
        if (split.intervalTimer <= 0.0f) {
            split.hasSplit = true; // 1回のみ
            BulletAttachmentLogic::SpawnChildBullet(
                _world, trans.position, rb.velocity, split.data, ctx.currentDamage, ctx);

            // 親弾は少し軌道を曲げる（旧コード準拠）
            Vector3 vel = rb.velocity;
            Matrix  rot = Matrix::CreateRotationY(
                DirectX::XMConvertToRadians(-split.data.angleSpread * 0.5f));
            rb.velocity = Vector3::TransformNormal(vel, rot);
        }
    });
}

void TravelChaosSystem::Update(float dt)
{
    ForEach([&](RigidBodyComponent &rb, TravelChaosComponent &chaos) {
        chaos.changeTimer -= dt;
        if (chaos.changeTimer <= 0.0f) {
            chaos.changeTimer   = 0.3f;
            float   randomAngle = GetRandomFloat(-45.0f, 45.0f);
            Vector3 vel         = rb.velocity;
            float   speed       = vel.Length();
            Matrix  rot         = Matrix::CreateRotationY(DirectX::XMConvertToRadians(randomAngle));
            vel                 = Vector3::TransformNormal(vel, rot);
            vel.Normalize();
            rb.velocity = vel * (speed * GetRandomFloat(1.0f, chaos.data.speedMultRange));
        }
    });
}




REGISTER_LOGIC_SYSTEM(TravelHomingSystem, Priority::LogicStage::Update);
REGISTER_LOGIC_SYSTEM(TravelAccelerateSystem, Priority::LogicStage::Update);
REGISTER_LOGIC_SYSTEM(TravelSlowSystem, Priority::LogicStage::Update);
REGISTER_LOGIC_SYSTEM(TravelExplosionSystem, Priority::LogicStage::Update);
REGISTER_LOGIC_SYSTEM(TravelSplitSystem, Priority::LogicStage::Update);
REGISTER_LOGIC_SYSTEM(TravelChaosSystem, Priority::LogicStage::Update);