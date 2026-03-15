#include "HealthSystem.h"
#include "Game/Logics/Combat/DeadTag.h"
#include "Game/Logics/Combat/GlobalDamageQueue.h"
#include "Game/Logics/GameEvent/GameEvents.h"


#include "Game/Logics/Character/Player/PlayerComponent.h"
#include <algorithm>

// システムの実行順序の定義ヘッダー
#include "ECS/System/CCL_SystemRegistry.h"
#include "Game/Core/SystemPriority.h"

void HealthSystem::Update(float dt)
{

    // =======================================================================
    // フェーズ1：郵便配達（ダメージイベントの一括処理）
    // =======================================================================
    // 郵便局が存在するかチェック
    if (_world->HasResource<GlobalDamageQueue>()) {
        auto &damageQueue = _world->GetResource<GlobalDamageQueue>();

        // 溜まっているすべてのダメージイベントを処理する
        for (const auto &dmg : damageQueue.events) {

            // 手紙の宛先（ターゲット）が HealthComponent を持っているか確認
            if (auto *health = _world->GetComponent<HealthComponent>(dmg.targetID)) {

                // 無敵時間中ならダメージを無視して次へ
                if (health->invincibilityTimer > 0.0f) {
                    continue;
                }

                // ダメージを適用し、無敵時間を開始する
                health->currentHealth -= dmg.amount;
                health->invincibilityTimer = health->invincibilityDuration;

                // =======================================================
                // ★追加：ダメージを受けたことをワールド全体に放送する
                // =======================================================
                DamageTakenEvent e;
                e.targetID     = dmg.targetID;
                e.damageAmount = dmg.amount;
                // このエンティティがプレイヤーコンポーネントを持っているかチェック
                e.isPlayer = _world->HasComponent<PlayerComponent>(dmg.targetID);

                _world->GetEventBus().Publish(e);
                // =======================================================



            }
        }

        // 全てのダメージを処理し終わったら、必ずキューを空にする！（超重要）
        damageQueue.Clear();
    }


    // =======================================================================
    // フェーズ2：毎日の健康診断（全エンティティの定期更新）
    // =======================================================================
    ForEachWithID([&](CCL::ECS::EntityID id, HealthComponent &health) {
        // 1. 無敵時間のカウントダウン
        if (health.invincibilityTimer > 0.0f) {
            health.invincibilityTimer -= dt;
        }

        // 2. 死亡判定と Tag の付与
        if (health.currentHealth <= 0.0f) {
            health.currentHealth = 0.0f;

            // エンティティにまだ DeadTag が付いていなければ付与する
            if (!_world->HasComponent<Tag::DeadTag>(id)) {
                _world->AddComponent<Tag::DeadTag>(id);
            }
        }
    });

}

REGISTER_LOGIC_SYSTEM(HealthSystem, Priority::LogicStage::Resolution);