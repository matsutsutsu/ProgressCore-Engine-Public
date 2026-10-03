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
    /**
     * @brief このドローンが突撃中で危険かを返す。
     *
     * - 発射待ち（fireDelay > 0）は危険ではない
     * - 陣形追従の突撃（縦割り・小隊など）は、突入中ずっと危険
     * - 直進系（弾幕）は、タメ（stateTimer > 0）が終わってからだけ危険
     *
     * 硬直（Stunned）と離脱（Returning）は FireCharge ではないので、状態の1行でまとめて除外される。
     * 「突き抜けた後の帰り道は当たらない」は状態の分離から自動的に出てくるもので、専用の判定は要らない。
     *
     * 吹き飛ばし中かは knockbackTimer を直接見る。
     * 切り払い（L02_PostUpdate）は判断層（L02_PreUpdate）より後に書くので、切り払われたフレームはまだ状態が FireCharge のまま。
     * ここで見れば同じフレームで判定が閉じる。
     *
     * @param d 判定する機体。
     * @return 当たり判定を開くべきなら true。
     */
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
    // 処理フロー:
    // 1. ダメージの帰属先（ボス）を決める
    // 2. 陣形ごとの当たり方を引く準備をする
    // 3. 機体ごとに判定を開け閉めし、当たり方を写す
    // 4. デバッグ表示用の機数を残す

    // --- 帰属先 ---

    // ダメージの帰属先はボス。ドローン個体にしない理由は次の2つ。
    //   - 撃破ログや被弾演出が「ボスにやられた」と読めること
    //   - 自傷防止（ownerID == 被弾者）の判定が意味を持つこと
    EntityID owner = InvalidEntityID;
    if (m_world->HasResource<BattleContext>())
        owner = m_world->GetResource<BattleContext>().bossEntity;

    // --- 当たり方のキャッシュ ---

    // 陣形ごとの当たり方を引く仮想呼び出しは、陣形の種類が変わったときだけにする。
    // 通常は全機が同じ陣形なので、1フレームあたり1回で済む。
    auto &registry = CCL::AI::Formation::FormationRegistry::Instance();

    bool                                cacheValid = false;
    DroneFormationType                  cacheType  = DroneFormationType::None;
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

    // --- 判定の開け閉め ---

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
        // HitboxComponent は「1回の攻撃で同じ相手を二度叩かない」ために命中済みの相手を覚えるので、
        // 捨てないと2回目の突撃が当たらなくなる。

        // 剣は攻撃アニメの開始で履歴を捨てる。ドローンでは危険区間の立ち上がりがそれに当たる。
        if (dangerous && !box.isActive) box.hitCount = 0;

        box.isActive = dangerous;
        box.ownerID  = owner;

        // ダメージの帰属はボスのまま、のけぞり・ノックバックの向きだけは
        // 実際に当てたこの機体から取らせる（機体はボスから数十m離れている）。
        box.useSelfAsHitOrigin = true;

        if (!dangerous) return;

        // 痛さは陣形が持つ（IFormation::GetCombat）。
        // 大技は痛く、牽制は軽い、という差をここで反映する。
        const auto &combat  = combatOf(drone.currentFormation);
        box.damageAmount    = combat.damage;
        box.knockback       = combat.knockback;
        box.hitStopDuration = combat.hitStopDuration;
        box.hitStopScale    = combat.hitStopScale;
        box.rehitInterval   = combat.rehitInterval;

        ++activeCount;
    });

    // --- デバッグ表示 ---

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

// 物理ステップ(L04)より前に判定の開け閉めを終わらせる。
REGISTER_LOGIC_SYSTEM(DroneHitSystem, Priority::LogicStage::L03_PrePhysics);
