// 突撃(FireCharge リリース)中のドローンがプレイヤーに接触したら DamageEvent を生成する実装。
//
// 実装の意図(Why):
//   - 既存の戦闘パイプラインへ「DamageEvent を1個積む」だけで合流する。HP減算・
//     ノックバック・ヒットストップ・被弾演出は DamageResolutionSystem(L06) 以降が担う。
//   - プレイヤーの無敵時間(HealthComponent.invincibilityTimer)が多段ヒットを吸収するので、
//     1フレームにつき DamageEvent は「最も近い危険ドローン1体」ぶんだけ生成すれば足りる
//     (50機同時接触でも無敵で1発に収束するため、無駄なイベント生成をしない)。
//   - 「危険」= リリース局面のみ。タメ(引き絞り)・発射待ち(fireDelay)は当たらない
//     = ジャスト回避で狙う「読ませ→避け」の窓と一致させる。
#include "DroneHitSystem.h"

#include "Game/Logics/AI/Battle/BattleContext.h"
#include "Game/Logics/AI/Drone/Formation/FormationRegistry.h"  // 陣形ごとの当たり方
#include "Game/Logics/Combat/CombatComponents.h"   // DamageEventComponent
#include "Game/Logics/Combat/HealthComponent.h"

#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/Common/CCL_Archetype.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include "Engine/Platform/Core/Logger.h"

#include <SimpleMath.h>
#include <cfloat>

using namespace CCL::ECS;
using DirectX::SimpleMath::Vector3;

namespace
{
    // 当たり半径・ダメージ・ノックバックは陣形ごとに持つ（IFormation::GetCombat）。
    //   以前はここに定数で置いていたため全陣形が同じ 2.5m / 8ダメージで、
    //   「大技は痛いが避けやすい／牽制は軽いが避けにくい」という強弱が作れなかった。
    //   既定値は当時の定数そのままなので、上書きしない陣形の挙動は変わらない。

    // このドローンが「今まさに突撃していて危険」か。
    //        タメ(stateTimer>0)・発射待ち(fireDelay>0)は危険ではない。
    //        陣形追従の突撃(縦割り等)は突入中ずっと危険、ダッシュ系はタメ後のみ危険。
    //
    // 硬直(Stunned)と離脱(Returning)は FireCharge ではないので、この1行で
    // まとめて除外される。「突き抜けた後の帰り道は当たらない」は状態の分離から
    // 自動的に出てくるもので、専用の判定は要らない。
    bool IsDangerous(const DroneComponent& d)
    {
        if (d.knockbackTimer > 0.0f)                  return false;   // 切り払いで吹き飛ばし中は無害
        if (d.currentState != DroneState::FireCharge) return false;   // Stunned / Returning もここで落ちる
        if (d.fireDelay > 0.0f)                       return false;   // 発射待ち中
        // 追従系（剛体・バネ）はタメを持たないので突入中ずっと危険。
        // 直進系だけがタメ（stateTimer>0）の間は無害。
        return d.chargeMode != ChargeMode::Straight || d.stateTimer <= 0.0f;
    }
}

void DroneHitSystem::Update(float /*dt*/)
{
    if (!m_world->HasResource<BattleContext>()) return;
    const auto& battle = m_world->GetResource<BattleContext>();

    const EntityID playerId = battle.playerEntity;
    if (playerId == InvalidEntityID) return;

    // プレイヤーが無敵中なら判定不要(多段ヒットは無敵で吸収されるため走査ごとスキップ)。
    auto* playerHealth = m_world->GetComponent<HealthComponent>(playerId);
    if (!playerHealth || playerHealth->invincibilityTimer > 0.0f) return;

    const Vector3 playerPos(battle.playerPos);

    // 陣形ごとの当たり方を引く。
    //   仮想呼び出しは「陣形の種類が変わったときだけ」に留める（IFormation.h の方針）。
    //   実際には全機が同じ陣形なので、1フレームあたり1回で済む。
    auto &registry = CCL::AI::Formation::FormationRegistry::Instance();

    bool                            cacheValid = false;
    DroneFormationType              cacheType  = DroneFormationType::Hidden;
    CCL::AI::Formation::FormationCombat cacheCombat{};

    auto combatOf = [&](DroneFormationType type) -> const CCL::AI::Formation::FormationCombat & {
        if (!cacheValid || cacheType != type)
        {
            const auto *impl = registry.Get(type);
            cacheCombat = impl ? impl->GetCombat() : CCL::AI::Formation::FormationCombat{};
            cacheType   = type;
            cacheValid  = true;
        }
        return cacheCombat;
    };

    // --- 最も近い「危険ドローン」を1体だけ探す ---
    //   半径が機体ごとに違いうるので、まず各機を自分の半径で足切りしてから
    //   距離を比べる。共通の半径で初期化して比較する以前の形は使えない。
    EntityID hitDrone    = InvalidEntityID;
    Vector3  hitDronePos = Vector3::Zero;
    float    bestDistSq  = FLT_MAX;
    CCL::AI::Formation::FormationCombat hitCombat{};

    ForEachWithID([&](EntityID id, const DroneComponent& d, const TransformComponent& t)
    {
        if (!IsDangerous(d)) return;

        const auto   &combat = combatOf(d.currentFormation);
        const Vector3 dpos(t.position);
        const float   distSq = (dpos - playerPos).LengthSquared();

        if (distSq > combat.hitRadius * combat.hitRadius) return; // この機の判定外
        if (distSq >= bestDistSq) return;

        bestDistSq  = distSq;
        hitDrone    = id;
        hitDronePos = dpos;
        hitCombat   = combat;
    });

    if (hitDrone == InvalidEntityID) return;   // 誰も接触していない

    // --- ノックバック方向(ドローン→プレイヤーのXZ。平面ステージなのでYは殺す) ---
    Vector3 knockDir   = playerPos - hitDronePos;
    knockDir.y         = 0.0f;
    float   knockForce = 0.0f;
    if (knockDir.LengthSquared() > 1e-6f)
    {
        knockDir.Normalize();
        knockForce = hitCombat.knockback;
    }

    // --- DamageEvent を1個生成(HitboxCollisionSystem と同じ合流点) ---
    static const Archetype damageArchetype = ArchetypeHelper::Generate<DamageEventComponent>();
    auto ref = m_world->Spawn(damageArchetype);

    DamageEventComponent dmg{};
    dmg.targetID        = playerId;
    dmg.attackerID      = hitDrone;
    dmg.finalDamage     = hitCombat.damage;
    dmg.knockbackDir    = knockDir;
    dmg.knockbackForce  = knockForce;
    dmg.hitStopDuration = hitCombat.hitStopDuration;
    dmg.hitStopScale    = hitCombat.hitStopScale;
    dmg.hitPosition     = (playerPos + hitDronePos) * 0.5f;
    ref.Set(dmg);

    CCL_LOG_INFO(LogCategory::Game,
        "[DroneHit] drone %llu -> player %llu dmg=%.1f r=%.1f", hitDrone, playerId,
        hitCombat.damage, hitCombat.hitRadius);
}

// L05_Collision: HitboxCollisionSystem と同じ判定ステージに置く。生成した DamageEvent は
//                L06_DamageApply の DamageResolutionSystem が消費して HP に反映する。
REGISTER_LOGIC_SYSTEM(DroneHitSystem, Priority::LogicStage::L05_Collision);
