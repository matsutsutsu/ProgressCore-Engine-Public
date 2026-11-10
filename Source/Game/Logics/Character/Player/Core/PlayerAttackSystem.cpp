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

// 押したときのスティックの向き。コンボの派生をグラフがこれで選ぶ
static const uint32_t HASH_PARAM_ATTACK_DIR = CCL::Utils::HashString("AttackDir");

namespace
{
    // 攻撃時に敵の方へ向く処理の定数。最大距離は PlayerComponent.attackFaceMaxDistance（エディタで調整できる）
    constexpr float kFaceDuration     = 0.22f;               // 敵の方へ向き直る時間（秒）
    constexpr float kBackFaceDuration = 0.8f;                // 後ろの技で敵を向いたまま下がる時間（秒）
    const float     kFaceConeCos      = std::cos(1.221730f); // 前方±70°の内積のしきい値（cos70°）

    // AttackDir の値。グラフの遷移の条件と同じ数
    constexpr float kAttackDirNeutral = 0.0f; // 倒していない・横
    constexpr float kAttackDirForward = 1.0f; // 敵の方へ
    constexpr float kAttackDirBack    = 2.0f; // 敵と反対へ

    constexpr float kStickThreshold = 0.5f;                // これより浅い傾きはニュートラル
    const float     kDirConeCos     = std::cos(0.872665f); // 前・後ろとみなす扇の半角（cos50°）
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
// 向き
// ---------------------------------------------------------------------------

bool PlayerAttackSystem::FindAttackTargetDir(EntityID playerID, const PlayerComponent &player, Vector3 &outDir)
{
    // 処理フロー:
    // 1. 向く対象を決める（ロックオン優先、なければ前方で最も近い敵）
    // 2. 対象がない／遠いなら false（今の向きのまま攻撃する）
    // 3. 対象への水平の向きを返す

    const auto *ptrans = m_world->GetComponent<TransformComponent>(playerID);
    if (!ptrans)
    {
        return false;
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
        return false;
    }

    const auto *etrans = m_world->GetComponent<TransformComponent>(target);
    if (!etrans)
    {
        return false;
    }
    Vector3 to = Vector3(etrans->position) - playerPos;
    to.y       = 0.0f;
    if (to.LengthSquared() < 1e-4f)
    {
        return false;
    }

    // ロックオン中でも attackFaceMaxDistance より遠い敵には向かず、入力方向のまま攻撃できるようにする
    if (to.LengthSquared() > player.attackFaceMaxDistance * player.attackFaceMaxDistance)
    {
        return false;
    }

    // ---------------------------------------------------------
    // 3. 向きを返す
    // ---------------------------------------------------------
    to.Normalize();
    outDir = to;
    return true;
}



float PlayerAttackSystem::ClassifyAttackDir(const Vector3 &baseDir, const CharacterMovementComponent &movement) const
{
    // 少し倒しただけでは、本線のまま出す
    if (movement.moveIntensity < kStickThreshold) return kAttackDirNeutral;

    Vector3 stick = movement.desiredVelocity;
    stick.y       = 0.0f;
    if (stick.LengthSquared() < 1e-4f) return kAttackDirNeutral;
    stick.Normalize();

    const float d = stick.Dot(baseDir);
    if (d >= kDirConeCos) return kAttackDirForward;
    if (d <= -kDirConeCos) return kAttackDirBack;

    // 横は、敵の周りを回りながら連打している。本線のまま出す
    return kAttackDirNeutral;
}



// ---------------------------------------------------------------------------
// 入口
// ---------------------------------------------------------------------------

void PlayerAttackSystem::Update(float dt)
{
    ForEachWithID([&](EntityID                          playerID,
                      const PlayerInputComponent       &pin,
                      AnimParametersComponent          &animParams,
                      PlayerComponent                  &player,
                      const PlayerRushComponent        &rush,
                      const CharacterMovementComponent &movement) {
        // ラッシュ中は通常攻撃を出さない
        //   ラッシュ中の攻撃入力は PlayerRushSystem が受け取り、ラッシュの段（RushAttack）として出す
        //   ここで通常攻撃まで出ると、Attack と RushAttack の2つのトリガーが同時に立つ
        if (rush.isActive)
        {
            return;
        }

        if (!pin.attackPressed) return;

        // 処理フロー:
        // 1. 前・後ろの基準を決める。敵がいれば敵の方、いなければ今の正面
        // 2. スティックの向きを AttackDir に書く
        // 3. 向き直る
        // 4. Attack トリガーを出す

        // ---------------------------------------------------------
        // 1. 基準
        // ---------------------------------------------------------

        Vector3    baseDir;
        const bool hasTarget = FindAttackTargetDir(playerID, player, baseDir);
        if (!hasTarget) baseDir = Vector3(std::sin(player.currentYaw), 0.0f, std::cos(player.currentYaw));

        // ---------------------------------------------------------
        // 2. スティックの向き
        // ---------------------------------------------------------

        // 押した瞬間の向きを残す。先行入力の間にスティックが戻っても、押したときの技が出る
        const float attackDir = ClassifyAttackDir(baseDir, movement);
        animParams.SetFloat(HASH_PARAM_ATTACK_DIR, attackDir);

        // ---------------------------------------------------------
        // 3. 向き直り（地上/空中どちらも。実際に回すのは PlayerJumpFacingSystem）
        // ---------------------------------------------------------

        // 後ろの技は敵を正面に置いたまま下がる。短いと、倒したままのスティックの方へ振り向いてしまう
        const bool back = (attackDir == kAttackDirBack);
        if (hasTarget || back)
        {
            player.attackFaceYaw   = std::atan2(baseDir.x, baseDir.z);
            player.attackFaceTimer = back ? kBackFaceDuration : kFaceDuration;
        }

        // ---------------------------------------------------------
        // 4. トリガー
        // ---------------------------------------------------------

        // 攻撃は常に Attack トリガー1つで出す（第2引数は先行入力として保持する秒数）
        //   通常 / 空中 / コンボ継続の分岐はアニメグラフが決め、コードはステート名を見ない
        animParams.SetTrigger(HASH_TRIG_ATTACK, 0.25f);
    });
}



// PlayerInputSystem(L01_Input) の後（GetRunAfter）、AnimControllerSystem(L02_Update) より前に走る
REGISTER_LOGIC_SYSTEM(PlayerAttackSystem, Priority::LogicStage::L01_Input);
