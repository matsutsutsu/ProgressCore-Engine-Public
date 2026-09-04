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
// 速度の平滑化にかける時定数 [秒]。
//   短いほど機敏に追従するが狙いが暴れる。長いほど安定するが読みが鈍る。
//   「急な方向転換は読み切れない」が回避の余地になるので、鈍いほうへ倒してある。
constexpr float kVelocitySmoothing = 0.12f;

// 追跡する速度の上限 [m/s]。テレポート等の異常値を弾くための蓋。
//   プレイヤーの走行速度(65.5)より十分上に取る。
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
    auto& battle = m_world->GetResource<BattleContext>();

    // 速度を出すために、上書きする前の座標を控えておく。
    const DirectX::SimpleMath::Vector3 prevPlayerPos(battle.playerPos);
    const bool hadPlayer = battle.HasPlayer();

    // --- プレイヤーを探して反映 ---
    
    // 1対1なので最初の1体でよい。毎フレーム走るため確保はしない。
    EntityID playerId = InvalidEntityID;
    m_world->ViewEach<PlayerComponent, TransformComponent>([&](EntityID e) {
        if (playerId == InvalidEntityID) playerId = e;
    });

    battle.playerEntity = playerId;
    if (playerId != InvalidEntityID) {
        if (auto* t = m_world->GetComponent<TransformComponent>(playerId)) {
            // TransformComponent.position は「親からの相対位置」（ローカル）。
            //   プレイヤーのプレハブは親を持つので、そのまま配ると親のぶんずれる。
            //   同ヘッダに「ロジック判定には GetWorldPosition() を使うこと」と
            //   明記されている。狙いも当たり判定もここを唯一の出どころにしている
            //   以上、ここが world でなければ全部がずれる。
            battle.playerPos = t->GetWorldPosition();

            // --- 体の中心を出す ---
            //   JoltCharacterSetupSystem がカプセルを載せるときのオフセットと
            //   同じ式にする。カプセルが無い場合は足元のまま（ずれない）。
            float centerLift = 0.0f;
            if (const auto *cap = m_world->GetComponent<JoltCapsuleColliderComponent>(playerId))
                centerLift = cap->halfHeight + cap->radius;

            battle.playerCenter = battle.playerPos;
            battle.playerCenter.y += centerLift;

            // --- 座標の差分から速度を出す ---
            //
            // 生の1フレーム差分をそのまま使わない理由:
            //   フレーム時間の揺れや物理ステップの粒度で値が跳ねる。それを
            //   そのまま偏差射撃に使うと、狙いが毎フレーム暴れて
            //   「たまたま当たる／たまたま外れる」になる。
            //   時定数 kVelocitySmoothing で均すと狙いが安定し、同時に
            //   「急に方向を変えた直後は読みが遅れる」＝回避が成立する。
            //
            // 上限を掛ける理由:
            //   リスポーンや場面転換で座標が飛ぶと、1フレームだけ数千 m/s の
            //   速度が出る。平滑化しても尾を引くので、その場で頭を抑える。
            if (hadPlayer && dt > 0.0001f) {
                DirectX::SimpleMath::Vector3 raw =
                    (DirectX::SimpleMath::Vector3(battle.playerPos) - prevPlayerPos) / dt;

                if (raw.LengthSquared() > kMaxTrackedSpeed * kMaxTrackedSpeed) {
                    raw.Normalize();
                    raw *= kMaxTrackedSpeed;
                }

                const float k = (std::min)(dt / kVelocitySmoothing, 1.0f);
                battle.playerVelocity =
                    DirectX::SimpleMath::Vector3::Lerp(
                        DirectX::SimpleMath::Vector3(battle.playerVelocity), raw, k);
            }
            else {
                battle.playerVelocity = {0, 0, 0};
            }
        }
    }
    

    // --- ボスを探して反映 ---
    // 今はボス１体だけなので最初の１体だけ取得
    // 今後複数体敵が出るので改良しておく
    EntityID bossId = InvalidEntityID;
    m_world->ViewEach<BossCommandComponent, TransformComponent>([&](EntityID e) {
        if (bossId == InvalidEntityID) bossId = e;
    });

    battle.bossEntity = bossId;
    if (bossId != InvalidEntityID) {
        if (auto* t = m_world->GetComponent<TransformComponent>(bossId)) {
            battle.bossPos = t->GetWorldPosition();   // 同上（ローカルではなくワールド）
        }
    }
    
}


// AI層(L02〜)が読むより前に「世界の事実」を確定させる。
//   L01_WorldState はそのための専用ステージ。入力受付(L01_Input)と混ぜないのは、
//   1フレームの段取りを「入力 → 事実 → 感知 → 判断 → 行動」と一列に読めるようにするため。
REGISTER_LOGIC_SYSTEM(BattleContextSyncSystem, Priority::LogicStage::L01_WorldState);
