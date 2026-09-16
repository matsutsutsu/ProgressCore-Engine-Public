// 危険な区間だけドローンの Hitbox を開ける実装。
// 役割・なぜ距離判定をやめたかは DroneHitSystem.h を参照。
#include "DroneHitSystem.h"

#include "Game/Logics/AI/Battle/BattleContext.h"
#include "Game/Logics/AI/Drone/Formation/FormationRegistry.h" // 陣形ごとの当たり方

#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

#include "imgui.h"

using namespace CCL::ECS;

namespace
{
    // このドローンが「今まさに突撃していて危険」か。
    //        タメ(stateTimer>0)・発射待ち(fireDelay>0)は危険ではない。
    //        陣形追従の突撃(縦割り等)は突入中ずっと危険、直進系はタメ後のみ危険。
    //
    // 硬直(Stunned)と離脱(Returning)は FireCharge ではないので、この1行で
    // まとめて除外される。「突き抜けた後の帰り道は当たらない」は状態の分離から
    // 自動的に出てくるもので、専用の判定は要らない。
    bool IsDangerous(const DroneComponent &d)
    {
        if (d.knockbackTimer > 0.0f) return false;                  // 切り払いで吹き飛ばし中は無害
        if (d.currentState != DroneState::FireCharge) return false; // Stunned / Returning もここで落ちる
        if (d.fireDelay > 0.0f) return false;                       // 発射待ち中
        // 追従系（剛体・バネ）はタメを持たないので突入中ずっと危険。
        // 直進系だけがタメ（stateTimer>0）の間は無害。
        return d.chargeMode != ChargeMode::Straight || d.stateTimer <= 0.0f;
    }
} // namespace

void DroneHitSystem::Update(float /*dt*/)
{
    // ダメージの帰属先はボス。ドローン個体にしないのは、
    //   ・撃破ログや被弾演出が「ボスにやられた」と読めること
    //   ・自傷防止(ownerID == 被弾者)の判定が意味を持つこと
    // の2つが理由。
    EntityID owner = InvalidEntityID;
    if (m_world->HasResource<BattleContext>())
        owner = m_world->GetResource<BattleContext>().bossEntity;

    // 陣形ごとの当たり方を引く。
    //   仮想呼び出しは「陣形の種類が変わったときだけ」に留める（IFormation.h の方針）。
    //   実際には全機が同じ陣形なので、1フレームあたり1回で済む。
    auto &registry = CCL::AI::Formation::FormationRegistry::Instance();

    bool                                cacheValid = false;
    DroneFormationType                  cacheType  = DroneFormationType::Hidden;
    CCL::AI::Formation::FormationCombat cacheCombat{};

    auto combatOf = [&](DroneFormationType type) -> const CCL::AI::Formation::FormationCombat & {
        if (!cacheValid || cacheType != type)
        {
            const auto *impl = registry.Get(type);
            cacheCombat      = impl ? impl->GetCombat() : CCL::AI::Formation::FormationCombat{};
            cacheType        = type;
            cacheValid       = true;
        }
        return cacheCombat;
    };

    int droneCount    = 0;
    int activeCount   = 0;
    int stateCount[5] = {0, 0, 0, 0, 0};

    ForEach([&](const DroneComponent &drone, HitboxComponent &box) {
        ++droneCount;
        {
            const int s = static_cast<int>(drone.currentState);
            if (s >= 0 && s < 5) ++stateCount[s];
        }

        const bool dangerous = IsDangerous(drone);

        // 判定が開いた瞬間に多段ヒットの履歴を捨てる。
        //   HitboxComponent は「1回の攻撃で同じ相手を二度叩かない」ために
        //   命中済みの相手を覚える。捨てないと2回目の突撃が当たらなくなる。
        //   剣は攻撃アニメの開始で捨てているが、ドローンは
        //   「危険区間の立ち上がり」がそれに当たる。
        if (dangerous && !box.isActive) box.hitCount = 0;

        box.isActive = dangerous;
        box.ownerID  = owner;

        // ダメージの帰属はボスのまま、のけぞり・ノックバックの向きだけは
        // 実際に当てたこの機体から取らせる（機体はボスから数十m離れている）。
        box.useSelfAsHitOrigin = true;

        if (!dangerous) return;

        // 痛さは陣形が持つ（IFormation::GetCombat）。
        //   大技は痛く、牽制は軽い、という差をここで反映する。
        const auto &combat  = combatOf(drone.currentFormation);
        box.damageAmount    = combat.damage;
        box.knockback       = combat.knockback;
        box.hitStopDuration = combat.hitStopDuration;
        box.hitStopScale    = combat.hitStopScale;

        ++activeCount;
    });

    m_droneCount  = droneCount;
    m_activeCount = activeCount;
    for (int i = 0; i < 5; ++i)
        m_stateCount[i] = stateCount[i];
}

void DroneHitSystem::OnGui()
{
    if (!ImGui::CollapsingHeader("Drone Hit (判定の開け閉め)")) return;

    ImGui::Text("ドローン %d 機", m_droneCount);
    ImGui::TextColored(m_activeCount > 0 ? ImVec4(1.0f, 0.7f, 0.5f, 1.0f)
                                         : ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
                       "判定が開いている機 %d", m_activeCount);

    ImGui::Text("状態  Idle %d  Move %d  Fire %d  Stun %d  Return %d",
                m_stateCount[0], m_stateCount[1], m_stateCount[2],
                m_stateCount[3], m_stateCount[4]);

    ImGui::Separator();
    ImGui::TextWrapped(
        "当たり判定そのものは Jolt が持つ（ドローンの Jolt Sphere Collider が "
        "BOSS_ATTACK レイヤーのセンサー）。ここは危険な区間だけ Hitbox を開ける係。\n"
        "・突撃中なのに『判定が開いている機』が 0 → 状態の判定がおかしい\n"
        "・開いているのに当たらない → 球の半径か、プレイヤー側の Hurtbox を疑う");
}

// L03_PrePhysics: 物理ステップ(L04)より前に判定の開け閉めを終わらせる。
//                 接触は Jolt が拾い、L05 の HitboxCollisionSystem が
//                 Hitbox×Hurtbox を見て DamageEvent を積む。
REGISTER_LOGIC_SYSTEM(DroneHitSystem, Priority::LogicStage::L03_PrePhysics);
