// プレイヤー・ボスを探して BattleContext に反映する実装。
#include "BattleContextSyncSystem.h"

#include "Game/Logics/AI/Battle/BattleContext.h"
#include "Game/Logics/Character/Boss/Components/BossCommandComponent.h"
#include "Engine/Physics/Components/Collision/JoltCapsuleColliderComponent.h"

#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

#include <algorithm>

using namespace CCL::ECS;

namespace
{
    // 速度の平滑化にかける時定数 [秒]。「急な方向転換は読み切れない」が回避の余地に
    // なるので、鈍いほうへ倒してある。
    constexpr float kVelocitySmoothing = 0.12f;

    // 追跡する速度の上限 [m/s]。テレポート等の異常値を弾く蓋（走行速度65.5より十分上）。
    constexpr float kMaxTrackedSpeed = 120.0f;
} // namespace

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

    // 速度を出すために、上書きする前の座標を控える。
    const DirectX::SimpleMath::Vector3 prevPlayerPos(battle.playerPos);
    const bool                         hadPlayer = battle.HasPlayer();

    // 1対1なので最初の1体でよい。毎フレーム走るので確保はしない。
    EntityID playerId = InvalidEntityID;
    m_world->ViewEach<PlayerComponent, TransformComponent>([&](EntityID e) {
        if (playerId == InvalidEntityID) playerId = e;
    });

    battle.playerEntity = playerId;
    if (playerId != InvalidEntityID)
    {
        if (auto *t = m_world->GetComponent<TransformComponent>(playerId))
        {
            // TransformComponent.position はローカル。プレイヤーのプレハブは親を持つので、
            // そのまま配ると親のぶんずれる。狙いも当たり判定もここが唯一の出どころ。
            battle.playerPos = t->GetWorldPosition();

            // 体の中心。JoltCharacterSetupSystem がカプセルを載せるときと同じ式にする。
            float centerLift = 0.0f;
            if (const auto *cap = m_world->GetComponent<JoltCapsuleColliderComponent>(playerId))
                centerLift = cap->halfHeight + cap->radius;

            battle.playerCenter = battle.playerPos;
            battle.playerCenter.y += centerLift;

            // 座標の差分から速度を出す。生の1フレーム差分はフレーム時間の揺れで跳ねるので、
            // そのまま偏差射撃に使うと狙いが「たまたま当たる／外れる」になる。均せば狙いが
            // 安定し、同時に「急に方向を変えた直後は読みが遅れる」＝回避が成立する。
            // 上限は、座標が飛んだときの数千 m/s が平滑化で尾を引かないようにする蓋。
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

    // ボスも今は1体だけなので最初の1体を取る。
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

// AI層(L02〜)が読むより前に「世界の事実」を確定させる。入力受付(L01_Input)と
// 混ぜないのは、1フレームの段取りを一列に読めるようにするため。
REGISTER_LOGIC_SYSTEM(BattleContextSyncSystem, Priority::LogicStage::L01_WorldState);
