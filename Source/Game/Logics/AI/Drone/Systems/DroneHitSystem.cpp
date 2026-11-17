#include "DroneHitSystem.h"

#include "Game/Logics/AI/Battle/BattleContext.h"
#include "Game/Logics/AI/Drone/Formation/FormationRegistry.h"
#include "Game/Logics/AI/Drone/Motion/DroneMotion.h"

#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

#include "imgui.h"

using namespace CCL::ECS;

namespace
{
    // 当たり判定を開くべきかを返す。突撃中だけ開き、その中の「当たらない区間」は動き方と技に聞く
    bool IsDangerous(const DroneComponent &d, const CCL::AI::Formation::IFormation *formation)
    {
        // 離脱（Returning）は FireCharge と別の状態なので、帰り道はここで自動的に当たらなくなる
        if (d.currentState != DroneState::FireCharge) return false;

        if (formation && formation->IsHitWindowOver(d)) return false;

        return CCL::AI::Motion::IsChargeDangerous(d);
    }
}

void DroneHitSystem::Update(float dt)
{
    // 処理フロー:
    // 1. ダメージの帰属先（ボス）を決める
    // 2. 機体ごとに判定を開け閉めし、陣形の当たり方を写す
    // 3. デバッグ表示用の機数を残す

    // --- 帰属先 ---

    // 個体ではなくボスにする。被弾演出が「ボスにやられた」と読め、自傷防止（ownerID == 被弾者）も効く
    EntityID owner = InvalidEntityID;
    if (m_world->HasResource<BattleContext>())
        owner = m_world->GetResource<BattleContext>().bossEntity;

    // --- 判定の開け閉め ---

    const auto *registry = CCL::AI::Formation::FormationRegistry::Find(*m_world);

    int droneCount    = 0;
    int activeCount   = 0;
    int stateCount[4] = {0, 0, 0, 0};

    ForEach([&](const DroneComponent &drone, HitboxComponent &box) {
        ++droneCount;
        {
            const int s = static_cast<int>(drone.currentState);
            if (s >= 0 && s < 4) ++stateCount[s];
        }

        const auto *formation = registry ? registry->Get(drone.currentFormation) : nullptr;
        const bool  dangerous = IsDangerous(drone, formation);

        // 判定が開いた瞬間に命中済みの履歴を捨てる。捨てないと2回目の突撃が同じ相手に当たらない
        if (dangerous && !box.isActive) box.hitCount = 0;

        box.isActive = dangerous;
        box.ownerID  = owner;

        // ノックバックの向きは、ボスではなく実際に当てたこの機体から取る
        box.useSelfAsHitOrigin = true;

        if (!dangerous) return;

        const auto combat   = formation ? formation->GetCombat() : CCL::AI::Formation::FormationCombat{};
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
    for (int i = 0; i < 4; ++i)
        m_stateCount[i] = stateCount[i];
}



void DroneHitSystem::OnGui()
{
    ImGui::Text("ドローン %d 機", m_droneCount);
    ImGui::TextColored(m_activeCount > 0 ? ImVec4(1.0f, 0.7f, 0.5f, 1.0f)
                                         : ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
                       "判定が開いている機 %d", m_activeCount);

    ImGui::Text("状態  Idle %d  Move %d  Fire %d  Return %d",
                m_stateCount[0], m_stateCount[1], m_stateCount[2], m_stateCount[3]);

    ImGui::Separator();
    ImGui::TextWrapped(
        "当たり判定そのものは Jolt が持つ（ドローンの Jolt Sphere Collider が "
        "BOSS_ATTACK レイヤーのセンサー）。ここは危険な区間だけ Hitbox を開ける係。\n"
        "・突撃中なのに『判定が開いている機』が 0 → 状態の判定がおかしい\n"
        "・開いているのに当たらない → 球の半径か、プレイヤー側の Hurtbox を疑う");
}



// 物理ステップ(L04)より前に判定の開け閉めを終わらせる
REGISTER_LOGIC_SYSTEM(DroneHitSystem, Priority::LogicStage::L03_PrePhysics);
