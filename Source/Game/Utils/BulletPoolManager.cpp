#include "BulletPoolManager.h"
#include "Engine/GamePlay/Physics/RigidBody/RigidBodyComponent.h"
#include "Engine/GamePlay/Transform/TransformComponent.h"
#include "PooledBulletComponent.h"

BulletPoolResult BulletPoolManager::WakeOrSpawn(
    Core::World &world, uint32_t keyHash, const Archetype &arch)
{
    std::vector<EntityID> &pool = _poolMap[keyHash];

    // 在庫がある場合は取り出す
    while (!pool.empty()) {
        EntityID id = pool.back();
        pool.pop_back();

        if (world.IsEntityValid(id)) {
            if (auto *p = world.GetComponent<PooledBulletComponent>(id)) {
                p->isActive = true;
            }
            // 「再利用」であることを返す
            return {id, false};
        }
    }

    // --- B. 新規作成 ---
    Archetype finalArch = arch;
    finalArch.Add<PooledBulletComponent>();

    EntityID id = world.RequestSpawnEntity(finalArch);

    PooledBulletComponent pc;
    pc.poolKeyHash = keyHash; // ハッシュ値を記録
    pc.isActive    = true;
    world.AddComponent<PooledBulletComponent>(id, pc);

    // 「新規作成」であることを返す
    return {id, true};
}

void BulletPoolManager::ReturnToPool(Core::World &world, EntityID id)
{
    auto *pc = world.GetComponent<PooledBulletComponent>(id);
    if (!pc) {
        world.Destroy(id);
        return;
    }

    // 既にプール待機状態なら二重登録を防ぐ
    if (!pc->isActive) return;

    pc->isActive = false;

    // 物理的に地下へ飛ばす（子供のパーティクルもTransformSystemで自動的についてくる）
    if (auto *trans = world.GetComponent<TransformComponent>(id)) {
        //trans->position = {0, 5.0f, 0};
        trans->position = {0, 1000.0f, 0};
        // 必要に応じて trans->UpdateMatrix() など
    }

    if (auto *rb = world.GetComponent<RigidBodyComponent>(id)) {
        rb->velocity = {0, 0, 0};
    }

    _poolMap[pc->poolKeyHash].push_back(id);
}