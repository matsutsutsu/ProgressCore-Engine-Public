// ===========================================================================
// DamageResolutionSystem.cpp
// --------------------------
// 役割・責務・パイプライン上の位置付けは DamageResolutionSystem.h を参照。
//
// 実装の意図(Why):
//   - DamageEvent を消費して HP を減算し、イベントを即破棄する「燃やして捨てる」
//     方式を踏襲。攻撃ソース(弾/剣/罠)に依存しない一点集約の解決点とする。
//   - ノックバックは常設の KnockbackComponent に velocity を書き込み isActive=true に
//     するだけ。AddComponent/RemoveComponent による構造変更(アーキタイプ移動)を避け、
//     弾幕で多数同時被弾しても再配置コストが発生しないようにする。
//   - 無敵中はダメージもノックバックも無効化する(多段ハメ防止の一貫性)。
// ===========================================================================
#include "DamageResolutionSystem.h"
#include "Game/Logics/Combat/CombatComponents.h"
#include "Game/Logics/Combat/HealthComponent.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include "Engine/Platform/Core/Logger.h"

using namespace CCL::ECS;

void DamageResolutionSystem::Update(float dt) {

    ForEachWithID([this](EntityID eventEntity, const DamageEventComponent& event) {

        if (auto* health = _world->GetComponent<HealthComponent>(event.targetID)) {

            // 無敵中はダメージ・ノックバックともに無効(イベントは破棄する)。
            if (health->invincibilityTimer <= 0.0f) {

                // --- HP減算 ---
                health->currentHealth -= event.finalDamage;
                health->invincibilityTimer = health->invincibilityDuration;

                CCL_LOG_INFO(LogCategory::Game,
                    "Entity %llu took %f damage%s. Remaining HP: %f",
                    event.targetID, event.finalDamage,
                    event.isCritical ? " (CRITICAL)" : "",
                    health->currentHealth);

                //// --- ノックバック付与(常設コンポーネントに書き込むだけ。構造変更なし) ---
                //// KnockbackComponent を持つ対象のみ反応する。持たない相手(壁等)は無視。
                //if (event.knockbackForce > 0.0f) {
                //    if (auto* kb = _world->GetComponent<KnockbackComponent>(event.targetID)) {
                //        // 連続被弾の暴走を避けるため最新の一撃で上書き(加算しない)。
                //        kb->velocity = event.knockbackDir * event.knockbackForce;
                //        kb->isActive = true;
                //    }
                //}
            }
        }

        // 処理が終わったイベントエンティティは必ず破棄する。
        _world->Destroy(eventEntity);
        });
}

REGISTER_LOGIC_SYSTEM(DamageResolutionSystem, Priority::LogicStage::L06_DamageApply);