// ===========================================================================
// DamageResolutionSystem.cpp
// --------------------------
// 役割・責務・パイプライン上の位置付けは DamageResolutionSystem.h を参照。
//
// 実装の意図(Why):
//   - DamageEvent を消費して HP を減算し、イベントを即破棄する「燃やして捨てる」
//     方式を踏襲。攻撃ソース(弾/剣/罠)に依存しない一点集約の解決点とする。
//   - ノックバックは常設の KnockbackComponent に velocity/timer を書き込むだけ。
//     AddComponent/RemoveComponent による構造変更(アーキタイプ移動)を避け、
//     多数同時被弾しても再配置コストが発生しないようにする。実際の移動・減衰は
//     JoltCharacterUpdateSystem が担う（CharacterVirtual 経由で壁も考慮）。
//   - 無敵中はダメージもノックバックも無効化する(多段ハメ防止の一貫性)。
// ===========================================================================
#include "DamageResolutionSystem.h"
#include "Game/Logics/Combat/HealthComponent.h"
#include "Engine/Timer/TimeState.h"
#include "Engine/Physics/Components/Character/KnockbackComponent.h"
#include "Engine/Graphics/Components/Impact/ScreenImpactComponent.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include "Engine/Platform/Core/Logger.h"

#include <algorithm>

using namespace CCL::ECS;

void DamageResolutionSystem::Update(float dt) {

    ForEachWithID([this](EntityID eventEntity, DamageEventComponent& event) {

        if (auto* health = _world->GetComponent<HealthComponent>(event.targetID)) {

            // 無敵中はダメージ・ノックバックともに無効(イベントは破棄する)。
            if (health->invincibilityTimer <= 0.0f) {

                // --- HP減算 ---
                health->currentHealth -= event.finalDamage;
                health->invincibilityTimer = health->invincibilityDuration;

                CCL_LOG_INFO(LogCategory::Game,
                    "Entity %llu took %f damage. Remaining HP: %f",
                    event.targetID, event.finalDamage,
                    health->currentHealth);

                // --- ヒットストップ適用（被弾側＋攻撃側の双方を止める）---
                // TimeScaleSystem(L01) が次フレームから hitStopTimer を消費し localDt=0 にする。
                if (event.hitStopDuration > 0.0f)
                {
                    if (auto *vt = _world->GetComponent<TimeStateComponent>(event.targetID))
                    {
                        vt->hitStopTimer = event.hitStopDuration;
                        vt->freezeScale  = event.hitStopScale;
                    }
                    if (auto *at = _world->GetComponent<TimeStateComponent>(event.attackerID))
                    {
                        at->hitStopTimer = event.hitStopDuration;
                        at->freezeScale  = event.hitStopScale;
                    }
                }

                // --- ノックバック適用 ---
                // 被弾側に KnockbackComponent が「常設」で付いていれば、命中方向×強さの
                // 速度を書き込む。実際の移動と減衰は JoltCharacterUpdateSystem が担う
                // （CharacterVirtual 経由なので壁抜けしない）。無ければ何もしない。
                if (event.knockbackForce > 0.0f)
                {
                    if (auto *kb = _world->GetComponent<KnockbackComponent>(event.targetID))
                    {
                        kb->velocity = event.knockbackDir * event.knockbackForce;
                        kb->timer    = kb->duration;
                    }
                }

                // --- 命中演出（画面）のトリガ ---
                // シーンに ScreenImpact singleton があれば、ダメージ量に応じた強さで
                // カメラシェイク＋PPバーストを起動する（VFXは直接呼ばずデータで流す方針）。
                // ScreenImpactSystem(R02) が減衰させ、camera / PostProcess へ反映する。
                if (auto *impact = _world->GetSingletonComponent<ScreenImpactComponent>())
                {
                    constexpr float kRefDamage = 25.0f; // これで power=1.0 の基準ダメージ
                    const float power = std::clamp(event.finalDamage / kRefDamage, 0.3f, 1.5f);
                    impact->Trigger(power, static_cast<float>(eventEntity & 0xFF) * 0.137f);
                }
            }
        }

        // 処理が終わったイベントエンティティは必ず破棄する。
        _world->Destroy(eventEntity);
        });
}

REGISTER_LOGIC_SYSTEM(DamageResolutionSystem, Priority::LogicStage::L06_DamageApply);