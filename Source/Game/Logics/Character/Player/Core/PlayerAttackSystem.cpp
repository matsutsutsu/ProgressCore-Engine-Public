#include "PlayerAttackSystem.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/Core/Math/StringHash.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include "Engine/Platform/Core/Logger.h"

#include "Engine/Transform/Components/TransformComponent.h"
#include "Game/Logics/Combat/Components/HealthComponent.h"
#include "Game/Logics/Combat/Components/DeadTag.h"

#include <SimpleMath.h>
#include <cmath>

using namespace CCL::ECS;
using namespace DirectX::SimpleMath;

// 攻撃入力を表すトリガーはこれ1つだけ。通常/ダッシュ/空中/コンボの分岐はアニメグラフが
//   パラメータ（CanDashAttack / IsAirborne など）とキャンセル区間で決めるため、
//   コードは攻撃の種類ごとのトリガーを出さない（どの攻撃かの知識をグラフにまとめる）
static const uint32_t HASH_TRIG_ATTACK = CCL::Utils::HashString("Attack");

namespace
{
    // 攻撃時に敵の方へ向く処理の定数。最大距離は PlayerComponent.attackFaceMaxDistance（エディタで調整できる）
    constexpr float kFaceDuration = 0.22f;               // 敵の方へ向き直る時間（秒）
    const float     kFaceConeCos  = std::cos(1.221730f); // 前方±70°の内積のしきい値（cos70°）
} // namespace

// ---------------------------------------------------------------------------
// 対象の探索
// ---------------------------------------------------------------------------

EntityID PlayerAttackSystem::FindBestFrontEnemy(EntityID selfID, const Vector3 &selfPos,
                                                const Vector3 &forward, float range, float coneCos)
{
    EntityID best       = InvalidEntityID;
    float    bestDistSq = range * range;

    // 候補を一覧に溜めず、走査しながら最も近いものを残す
    m_world->ViewEach<HealthComponent, TransformComponent>([&](EntityID id) {
        if (id == selfID)
        {
            return;
        }
        if (m_world->HasComponent<PlayerComponent>(id)) // プレイヤーは除く
        {
            return;
        }
        if (m_world->HasComponent<Tag::DeadTag>(id))
        {
            return;
        }

        const auto *hp = m_world->GetComponent<HealthComponent>(id);
        if (hp && hp->currentHealth <= 0.0f)
        {
            return;
        }
        const auto *tr = m_world->GetComponent<TransformComponent>(id);
        if (!tr)
        {
            return;
        }

        Vector3 to         = Vector3(tr->position) - selfPos;
        to.y               = 0.0f;
        const float distSq = to.LengthSquared();
        if (distSq < 1e-4f || distSq > bestDistSq)
        {
            return;
        }

        Vector3 dir = to;
        dir.Normalize();
        if (dir.Dot(forward) < coneCos) // 前方コーンの外は無視
        {
            return;
        }

        bestDistSq = distSq;
        best       = id;
    });
    return best;
}



// ---------------------------------------------------------------------------
// 向き直り
// ---------------------------------------------------------------------------

void PlayerAttackSystem::SetAttackFacing(EntityID playerID, PlayerComponent &player)
{
    // 処理フロー:
    // 1. 向く対象を決める（ロックオン優先、なければ前方で最も近い敵）
    // 2. 対象がない／遠いなら向かない（今の向きのまま攻撃する）
    // 3. 対象方向の yaw を書き、向き直りの時間を立てる

    const auto *ptrans = m_world->GetComponent<TransformComponent>(playerID);
    if (!ptrans)
    {
        return;
    }
    const Vector3 playerPos(ptrans->position);

    // ---------------------------------------------------------
    // 1. 対象を決める
    // ---------------------------------------------------------
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

    // ---------------------------------------------------------
    // 2. 向かない条件
    // ---------------------------------------------------------
    if (target == InvalidEntityID) // 敵がいなければ今の向きのまま攻撃する
    {
        return;
    }

    const auto *etrans = m_world->GetComponent<TransformComponent>(target);
    if (!etrans)
    {
        return;
    }
    Vector3 to = Vector3(etrans->position) - playerPos;
    to.y       = 0.0f;
    if (to.LengthSquared() < 1e-4f)
    {
        return;
    }

    // ロックオン中でも attackFaceMaxDistance より遠い敵には向かず、入力方向のまま攻撃できるようにする
    if (to.LengthSquared() > player.attackFaceMaxDistance * player.attackFaceMaxDistance)
    {
        return;
    }

    // ---------------------------------------------------------
    // 3. yaw を書く（実際に向きを変えるのは PlayerJumpFacingSystem）
    // ---------------------------------------------------------
    player.attackFaceYaw   = std::atan2(to.x, to.z); // PlayerJumpFacingSystem と同じ atan2(x,z) 規約
    player.attackFaceTimer = kFaceDuration;
}



// ---------------------------------------------------------------------------
// 入口
// ---------------------------------------------------------------------------

void PlayerAttackSystem::Update(float dt)
{
    ForEachWithID([&](EntityID                    playerID,
                      const PlayerInputComponent &pin,
                      AnimParametersComponent    &animParams,
                      PlayerComponent            &player,
                      const PlayerRushComponent        &rush) {
        // ラッシュ中は通常攻撃を出さない
        //   ラッシュ中の攻撃入力は PlayerRushSystem が受け取り、ラッシュの段（RushAttack）として出す
        //   ここで通常攻撃まで出ると、Attack と RushAttack の2つのトリガーが同時に立つ
        if (rush.isActive)
        {
            return;
        }

        if (pin.attackPressed)
        {
            // 攻撃の瞬間に対象の方へ向く（地上/空中どちらも）
            SetAttackFacing(playerID, player);

            // 攻撃は常に Attack トリガー1つで出す（第2引数は先行入力として保持する秒数）
            //   通常 / 空中 / コンボ継続の分岐はアニメグラフが決め、コードはステート名を見ない
            animParams.SetTrigger(HASH_TRIG_ATTACK, 0.25f);
        }
    });
}



// PlayerInputSystem(L01_Input) の後（GetRunAfter）、AnimControllerSystem(L02_Update) より前に走る
REGISTER_LOGIC_SYSTEM(PlayerAttackSystem, Priority::LogicStage::L01_Input);
