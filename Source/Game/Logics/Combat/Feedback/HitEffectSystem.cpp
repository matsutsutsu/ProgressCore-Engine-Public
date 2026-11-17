#include "Game/Logics/Combat/Feedback/HitEffectSystem.h"

#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

#include "Engine/Graphics/Systems/Particle/VFXSystem.h"
#include "Game/Core/GameEvents.h"
#include "Game/Logics/Character/Player/Core/PlayerComponent.h"
#include "Game/Logics/Character/Player/Rush/PlayerRushComponent.h"

using namespace CCL::ECS;

namespace
{
    // プレイヤーの攻撃が敵に当たったときに出すプレハブ
    const char *kHitSparkPrefab = "Assets/Prefabs/Particles/VFX_HitSpark";

    // 敵（ボス・ドローン）の攻撃がプレイヤーに当たったときに出すプレハブ
    const char *kEnemyHitPrefab = "Assets/Prefabs/Particles/VFX_EnemyHit";

    // ラッシュ中のプレイヤーの命中で、通常の火花の代わりに出すプレハブ
    const char *kRushHitPrefab       = "Assets/Prefabs/Particles/VFX_RushHit";       // 詰めの斬りと連撃
    const char *kRushFinishHitPrefab = "Assets/Prefabs/Particles/VFX_RushFinishHit"; // とどめ
}



void HitEffectSystem::Initialize()
{
    // 当たったという通知を受けて、その位置にパーティクルを1回出す
    ListenEvent<HitImpactEvent>([this](const HitImpactEvent &e) {

        // 攻撃している状態を見てヒットパーティクルのプレハブを選ぶ
        const std::string prefab = PickHitPrefab(e.attackerID);
        VFX::Play(*m_world, prefab, e.position);

        });
}



void HitEffectSystem::Update(float dt)
{
    // 何もしない。イベントを購読するために System にしている
}



// 攻撃した側の状態から、出すパーティクルのプレハブを選ぶ
// ※設計的には、攻撃ごとに差し替える仕組みを作るべきだけど
// 　ラッシュ以外で差し替える予定がないので実装に最適なこの形にしている！
const char *HitEffectSystem::PickHitPrefab(EntityID attackerID) const
{
    // 攻撃した側がプレイヤーでなければ、敵の攻撃用のパーティクル
    if (!m_world->HasComponent<PlayerComponent>(attackerID)) return kEnemyHitPrefab;

    // ラッシュ中のプレイヤーでなければ、通常の火花パーティクル
    const auto *rush = m_world->GetComponent<PlayerRushComponent>(attackerID);
    if (!rush || !rush->isActive) return kHitSparkPrefab;

    // ラッシュの最後のタイミングだった場合、入った後の命中はすべてとどめとして扱う
    // finishEntered は PlayerRushSystem が、とどめのアニメに入った瞬間に立てる
    return rush->finishEntered ? kRushFinishHitPrefab : kRushHitPrefab;
}



// 処理はイベントで動くので、ステージはダメージの処理（L06）の後であればよい
REGISTER_LOGIC_SYSTEM(HitEffectSystem, Priority::LogicStage::L07_Cleanup);
