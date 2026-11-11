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

// 攻撃入力を表すトリガー
static const uint32_t HASH_TRIG_ATTACK = CCL::Utils::HashString("Attack");

// 押したときのスティックの向き
static const uint32_t HASH_PARAM_ATTACK_DIR = CCL::Utils::HashString("AttackDir");

namespace
{
    // AttackDir の値。グラフの遷移の条件と同じ数
    //   調整値ではない（変えるとグラフの条件と食い違う）。向き直り・扇・先行入力の調整値は PlayerComponent が持つ
    constexpr float kAttackDirNeutral = 0.0f; // 倒していない・横
    constexpr float kAttackDirForward = 1.0f; // 敵の方へ
    constexpr float kAttackDirBack    = 2.0f; // 敵と反対へ
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
        if (id == selfID) return;
        if (m_world->HasComponent<PlayerComponent>(id)) return; // プレイヤーは除く
        if (m_world->HasComponent<Tag::DeadTag>(id)) return;

        const auto *hp = m_world->GetComponent<HealthComponent>(id);
        if (hp && hp->currentHealth <= 0.0f) return;
        const auto *tr = m_world->GetComponent<TransformComponent>(id);
        if (!tr) return;

        Vector3 to         = Vector3(tr->position) - selfPos;
        to.y               = 0.0f;
        const float distSq = to.LengthSquared();

        // 重なっている（向きが決まらない）か、今いちばん近い敵より遠いなら飛ばす
        if (distSq < 1e-4f || distSq > bestDistSq) return;

        Vector3 dir = to;
        dir.Normalize();
        if (dir.Dot(forward) < coneCos) return; // 前方コーンの外は無視

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
    if (!ptrans) return false;
    const Vector3 playerPos(ptrans->position);

    // ---------------------------------------------------------
    // 1. 対象を決める
    // ---------------------------------------------------------
    EntityID target = InvalidEntityID;

    // ロックオンしたとき
    if (player.lockOnTargetID != InvalidEntityID &&
        m_world->IsEntityValid(player.lockOnTargetID) &&
        !m_world->HasComponent<Tag::DeadTag>(player.lockOnTargetID))
    {
        target = player.lockOnTargetID;
    }
    else // ロックオンしていない時
    {        
        const Vector3 forward(std::sin(player.currentYaw), 0.0f, std::cos(player.currentYaw));

        // 前方の円錐の中で最も近い敵を探す。いなければ InvalidEntityID
        // ラジアンへの変換とcos計算を分ける
        const float coneAngleRad = DirectX::XMConvertToRadians(player.attackFaceConeAngle);

        // 正面に近いほど値が大きくなるように、cos を使う。cos(0)=1, cos(90)=0, cos(180)=-1
        // 後の処理で この値の大小を比較して、扇の中にいるかどうかを判定する
        const float coneCos      = std::cos(coneAngleRad);

        // 最も近い敵を探す
        target = FindBestFrontEnemy(playerID, playerPos, forward, player.attackFaceMaxDistance, coneCos);
    }

    // ---------------------------------------------------------
    // 2. 向かない条件
    // ---------------------------------------------------------
    // 敵がいなければ今の向きのまま攻撃する
    if (target == InvalidEntityID)  return false;

    const auto *etrans = m_world->GetComponent<TransformComponent>(target);
    if (!etrans) return false;
    
    Vector3 to = Vector3(etrans->position) - playerPos;
    to.y       = 0.0f;
    if (to.LengthSquared() < 1e-4f) return false;
    

    // ロックオン中でも attackFaceMaxDistance より遠い場合は向かない
    const float maxDistSq = player.attackFaceMaxDistance * player.attackFaceMaxDistance;
    if (to.LengthSquared() > maxDistSq) return false;

    // ---------------------------------------------------------
    // 3. 向きを返す
    // ---------------------------------------------------------
    to.Normalize();
    outDir = to;
    return true;
}


// baseDirに対してのスティックの向きから、前・後ろ・それ以外を返す
float PlayerAttackSystem::ClassifyAttackDir(const Vector3 &baseDir, const PlayerComponent &player,
                                            const CharacterMovementComponent &movement) const
{
    // 少し倒しただけでは、変更しないようにする。　デッドゾーン
    if (movement.moveIntensity < player.attackStickThreshold) return kAttackDirNeutral;

    Vector3 stick = movement.desiredVelocity;
    stick.y       = 0.0f;
    if (stick.LengthSquared() < 1e-4f) return kAttackDirNeutral;
    stick.Normalize();

    // cosで扇形の範囲にいるかどうかの基準の値を計算
    const float dirConeCos = std::cos(DirectX::XMConvertToRadians(player.attackDirConeAngle));
    const float d          = stick.Dot(baseDir);
    // 扇形の基準の値より大きければ前、負の値が小さければ後ろ、それ以外は横
    if (d >= dirConeCos) return kAttackDirForward;
    if (d <= -dirConeCos) return kAttackDirBack;

    // 横は、敵の周りを回りながら連打している。
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
        // ラッシュ中の攻撃入力は PlayerRushSystem が受け取るため
        if (rush.isActive) return;
        
        // 攻撃を押していないなら何もしない
        if (!pin.attackPressed) return;

        // 処理フロー:
        // 1. 前・後ろの基準を決める。敵がいれば敵の方、いなければ今の正面
        // 2. スティックの向きを AttackDir に書く
        // 3. 向き直る
        // 4. Attack トリガーを出す

        // ---------------------------------------------------------
        // 1. 基準
        // ---------------------------------------------------------

        // 攻撃対象の方向を基準にする。いなければ今の正面
        Vector3    baseDir;
        // 攻撃する方向を取得する
        const bool hasTarget = FindAttackTargetDir(playerID, player, baseDir);
        if (!hasTarget) baseDir = Vector3(std::sin(player.currentYaw), 0.0f, std::cos(player.currentYaw));

        // ---------------------------------------------------------
        // 2. スティックの向き
        // ---------------------------------------------------------

        // 押した瞬間の向きを残す。先行入力の間にスティックが戻っても、押したときの技が出る
        const float attackDir = ClassifyAttackDir(baseDir, player, movement);
        animParams.SetFloat(HASH_PARAM_ATTACK_DIR, attackDir);

        // ---------------------------------------------------------
        // 3. 向き直り（地上/空中どちらも。実際に回すのは PlayerJumpFacingSystem）
        // ---------------------------------------------------------

        // 後ろの技は敵を正面に置いたまま下がる。短いと、倒したままのスティックの方へ振り向いてしまう
        const bool back = (attackDir == kAttackDirBack);
        if (hasTarget || back)
        {
            // baseDirの基準方向をYawに書いた状態で、PlayerJumpFacingSystem が回す
            player.attackFaceYaw   = std::atan2(baseDir.x, baseDir.z);
            player.attackFaceTimer = back ? player.attackBackFaceDuration : player.attackFaceDuration;
        }

        // ---------------------------------------------------------
        // 4. トリガー
        // ---------------------------------------------------------

        // 攻撃は常に Attack トリガー1つで出す
        // コンボ遷移とかはアニメグラフ側が決めるのでAttackだけでいい
        animParams.SetTrigger(HASH_TRIG_ATTACK, player.attackInputBuffer);
    });
}



// Attack トリガーを読む AnimControllerSystem(L02_Update) の入力を確定させるので L02_PreUpdate に置く
//   入力の確定（PlayerInputSystem, L01_Input）はステージが前なので、順序の宣言は要らない
REGISTER_LOGIC_SYSTEM(PlayerAttackSystem, Priority::LogicStage::L02_PreUpdate);
