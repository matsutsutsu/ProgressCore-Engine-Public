#include "PlayerAttackSystem.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/Core/Math/StringHash.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include "Engine/Platform/Core/Logger.h"

#include "Engine/Transform/Components/TransformComponent.h" // 位置から敵方向を求める
#include "Game/Logics/Combat/HealthComponent.h"             // 敵判定（生存中の敵）
#include "Game/Logics/Combat/DeadTag.h"                     // 死亡除外

#include <SimpleMath.h>
#include <cmath>

using namespace CCL::ECS;
using namespace DirectX::SimpleMath;

// 攻撃の意思を表す唯一のトリガー。通常/ダッシュ/空中/コンボの分岐はグラフ側が
//   パラメータ(Speed/IsAirborne/IsDashing)とキャンセル窓で判断するため、
//   コードは DashAttack / AirAttack のトリガーを積まない（分岐知識をデータに一元化）。
static const uint32_t HASH_TRIG_ATTACK = CCL::Utils::HashString("Attack");

namespace
{
    // --- オートフェイシングのチューニング（定数のみ：変更頻度が低いため file-scope に置く） ---
    constexpr float kFaceDuration = 0.22f;               // 攻撃時に敵へ回頭させる時間(秒)
    const float     kFaceConeCos  = std::cos(1.221730f); // 前方±70°の内積閾値（cos70°）
    //   ※自動フェイシングの最大距離は PlayerComponent.attackFaceMaxDistance（エディタ調整可）を使う。
} // namespace

// プレイヤーの前方の一番近い敵を探す処理
EntityID PlayerAttackSystem::FindBestFrontEnemy(EntityID selfID, const Vector3 &selfPos,
                                                const Vector3 &forward, float range, float coneCos)
{
    EntityID best       = InvalidEntityID;
    float    bestDistSq = range * range;

    // 候補はドローンぶんだけ増えるので、一覧に貯めずその場で絞り込む。
    m_world->ViewEach<HealthComponent, TransformComponent>([&](EntityID id) {
        if (id == selfID) return;
        if (m_world->HasComponent<PlayerComponent>(id)) return; // 味方（プレイヤー）は除外
        if (m_world->HasComponent<Tag::DeadTag>(id)) return;

        const auto *hp = m_world->GetComponent<HealthComponent>(id);
        if (hp && hp->currentHealth <= 0.0f) return;
        const auto *tr = m_world->GetComponent<TransformComponent>(id);
        if (!tr) return;

        Vector3 to         = Vector3(tr->position) - selfPos;
        to.y               = 0.0f;
        const float distSq = to.LengthSquared();
        if (distSq < 1e-4f || distSq > bestDistSq) return;

        Vector3 dir = to;
        dir.Normalize();
        if (dir.Dot(forward) < coneCos) return; // 前方コーンの外は無視

        bestDistSq = distSq; // より近い敵を採用
        best       = id;
    });
    return best;
}

void PlayerAttackSystem::SetAttackFacing(EntityID playerID, PlayerComponent &player)
{
    const auto *ptrans = m_world->GetComponent<TransformComponent>(playerID);
    if (!ptrans) return;
    const Vector3 playerPos(ptrans->position);

    // 1. 対象決定：ロックオン中の敵を最優先。無ければ前方最寄りの敵。
    EntityID target = InvalidEntityID;
    if (player.lockOnTargetID != InvalidEntityID &&
        m_world->IsEntityValid(player.lockOnTargetID) &&
        !m_world->HasComponent<Tag::DeadTag>(player.lockOnTargetID))
    {
        target = player.lockOnTargetID;
    }
    else
    {
        const Vector3 forward(std::sin(player.currentYaw), 0.0f, std::cos(player.currentYaw));
        target = FindBestFrontEnemy(playerID, playerPos, forward, player.attackFaceMaxDistance, kFaceConeCos);
    }
    if (target == InvalidEntityID) return; // 敵が居なければ向いてる方向のまま攻撃

    // 2. 対象方向の yaw を求めて回頭を予約（実際の回頭は PlayerMoveSystem が担う）。
    const auto *etrans = m_world->GetComponent<TransformComponent>(target);
    if (!etrans) return;
    Vector3 to = Vector3(etrans->position) - playerPos;
    to.y       = 0.0f;
    if (to.LengthSquared() < 1e-4f) return;

    // 近いときだけ向く：対象が attackFaceMaxDistance より遠ければ回頭しない。
    //   ロックオン中でも遠い敵には勝手に向かず、入力方向のまま攻撃できる（遠距離での暴発防止）。
    if (to.LengthSquared() > player.attackFaceMaxDistance * player.attackFaceMaxDistance) return;

    player.attackFaceYaw   = std::atan2(to.x, to.z); // PlayerMoveSystem と同じ atan2(x,z) 規約
    player.attackFaceTimer = kFaceDuration;
}

void PlayerAttackSystem::Update(float dt)
{
    ForEachWithID([&](EntityID                    playerID,
                      const PlayerInputComponent &pin,
                      DashComponent              &dash,
                      AnimParametersComponent    &animParams,
                      PlayerComponent            &player,
                      const CleaveComponent      &cleave) {
        // 攻撃ボタンは Idle のときだけ「攻撃」を意味する。Ready/Lunging 中の解釈は
        // PlayerCleaveSystem が持つ（対応表は CleavePhase の宣言を参照）。
        if (cleave.phase != CleavePhase::Idle)
            return;

        if (pin.attackPressed)
        {
            // 攻撃の瞬間に対象へ回頭を予約（地上/空中どちらも）。
            SetAttackFacing(playerID, player);

            // 攻撃の意思は常に単一の Attack トリガーで表現する。
            //   「通常 / ダッシュ攻撃 / 空中攻撃 / コンボ継続」の分岐は、アニメグラフが
            //   パラメータ(Speed / IsAirborne / IsDashing)とキャンセル窓で判断する。
            //   これで“どの攻撃か”の知識をグラフ(データ)へ一元化し、コードは状態名を見ない。
            //   → ダッシュ攻撃後に速度が残っていても Attack を積み続けるだけで
            //      DashAttack → Attack_1 → Attack_2 がグラフの遷移で自然に繋がる。
            animParams.SetTrigger(HASH_TRIG_ATTACK, 0.25f);

            // ダッシュ中に攻撃したら、ダッシュの無敵・ジャスト回避受付を打ち切り、
            // 再ダッシュの暴発を防ぐクールダウンを入れる（グラフが DashAttack を選ぶ際の側効果）。
            // ※状態名の直書きではなく、コンポーネントの実状態 dash.isDashing に基づく処理。
            if (dash.isDashing)
            {
                dash.isDashing               = false;
                dash.isInvincible            = false;
                dash.isJustEvadeWindowActive = false;
                dash.currentTimer            = dash.dashCooldown;
            }
        }
    });
}

// 実行タイミング：PlayerInputSystem(L01_Input)の直後、AnimControllerSystem(L02_Update)より前
REGISTER_LOGIC_SYSTEM(PlayerAttackSystem, Priority::LogicStage::L01_Input);
