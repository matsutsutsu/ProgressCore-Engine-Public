#include "BattleContextSyncSystem.h"

#include "Game/Logics/AI/Battle/BattleContext.h"
#include "Game/Logics/Character/Boss/Components/BossCommandComponent.h"
#include "Engine/Physics/Components/Collision/JoltCapsuleColliderComponent.h"

#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

#include <algorithm>
#include <imgui.h>

using namespace CCL::ECS;

namespace
{
    // 速度の平滑化の時定数（秒）。急な方向転換に AI の予測が遅れることがプレイヤーの回避の余地に
    // なるので、反応を遅めにしている
    constexpr float kVelocitySmoothing = 0.12f;

    // 追跡する速度の上限（m/s）。テレポートなどの異常値を抑える（Player.json の走行速度 65.5 より十分に大きくする）
    constexpr float kMaxTrackedSpeed = 120.0f;
}

void BattleContextSyncSystem::Initialize()
{
    EnsureContext();
}



void BattleContextSyncSystem::EnsureContext()
{
    if (m_world->HasResource<BattleContext>()) return;
    m_world->AddResource<BattleContext>();
}



void BattleContextSyncSystem::Update(float dt)
{
    EnsureContext();
    auto &battle = m_world->GetResource<BattleContext>();

    // 速度を求めるために、上書きする前の座標を取っておく
    const DirectX::SimpleMath::Vector3 prevPlayerPos(battle.playerPos);
    const bool                         hadPlayer = battle.HasPlayer();

    // 1対1なので最初の1体でよい。毎フレーム走るので確保はしない
    EntityID playerId = InvalidEntityID;
    m_world->ViewEach<PlayerComponent, TransformComponent>([&](EntityID e) {
        if (playerId == InvalidEntityID) playerId = e;
    });

    battle.playerEntity = playerId;
    if (playerId != InvalidEntityID)
    {
        if (const auto *player = m_world->GetComponent<PlayerComponent>(playerId))
            battle.playerRunSpeed = player->runSpeed;

        // 回避中か、終わってから何秒か。回避中は経過を 0 に留め、終わった瞬間から数え始める
        const auto *dash     = m_world->GetComponent<PlayerDashComponent>(playerId);
        battle.playerDashing = dash && dash->isDashing;
        if (battle.playerDashing)
            battle.playerSinceDashEnd = 0.0f;
        else
            battle.playerSinceDashEnd += dt;

        if (auto *t = m_world->GetComponent<TransformComponent>(playerId))
        {
            // TransformComponent.position はローカル。プレイヤーのプレハブは親を持つので、
            // そのまま使うと親の分ずれる。AI の狙いも当たり判定も、プレイヤーの位置はここの値を使う
            battle.playerPos = t->GetWorldPosition();

            // 体の中心。JoltCharacterSetupSystem がカプセルを置くときと同じ式にする
            float centerLift = 0.0f;
            if (const auto *cap = m_world->GetComponent<JoltCapsuleColliderComponent>(playerId))
                centerLift = cap->halfHeight + cap->radius;

            battle.playerCenter = battle.playerPos;
            battle.playerCenter.y += centerLift;

            // 座標の差から速度を求める。1フレームの差だけではフレーム時間の揺れで値が跳ね、
            // 予測して狙うと当たるかどうかが偶然で決まる。平滑化すると狙いが安定し、
            // 同時に急に方向を変えた直後は予測が遅れるので、プレイヤーが避けられる
            // 上限は、座標が飛んだときの数千 m/s が平滑化で長く残らないようにするため
            if (hadPlayer && dt > 0.0001f)
            {
                DirectX::SimpleMath::Vector3 raw =
                    (DirectX::SimpleMath::Vector3(battle.playerPos) - prevPlayerPos) / dt;

                if (raw.LengthSquared() > kMaxTrackedSpeed * kMaxTrackedSpeed)
                {
                    raw.Normalize();
                    raw *= kMaxTrackedSpeed;
                }

                const float k = (std::min)(dt / kVelocitySmoothing, 1.0f);
                battle.playerVelocity =
                    DirectX::SimpleMath::Vector3::Lerp(
                        DirectX::SimpleMath::Vector3(battle.playerVelocity), raw, k);
            }
            else
            {
                battle.playerVelocity = {0, 0, 0};
            }
        }
    }

    // ボスも今は1体だけなので最初の1体を取る
    EntityID bossId = InvalidEntityID;
    m_world->ViewEach<BossCommandComponent, TransformComponent>([&](EntityID e) {
        if (bossId == InvalidEntityID) bossId = e;
    });

    battle.bossEntity = bossId;
    if (bossId != InvalidEntityID)
    {
        if (auto *t = m_world->GetComponent<TransformComponent>(bossId))
        {
            battle.bossPos = t->GetWorldPosition(); // 同上（ローカルではなくワールド）
        }
    }
}



void BattleContextSyncSystem::OnGui()
{
    if (!m_world->HasResource<BattleContext>()) return;
    const auto &battle = m_world->GetResource<BattleContext>();

    ImGui::Text("プレイヤーの回避: %s", battle.playerDashing ? "回避中" : "-");
    ImGui::Text("回避が終わってから: %.2f 秒", battle.playerSinceDashEnd);
}



// AI の System(L02〜) が読むより前に書き終える。入力(L01_Input)と別のステージにしているのは、
// 1フレームの処理の順番を一列に読めるようにするため
REGISTER_LOGIC_SYSTEM(BattleContextSyncSystem, Priority::LogicStage::L01_WorldState);
