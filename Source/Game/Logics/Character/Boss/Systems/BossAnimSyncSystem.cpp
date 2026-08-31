/**
 * ボスの移動アニメ駆動の実装。BossActionComponent → AnimParameters の翻訳。
 */
#include "BossAnimSyncSystem.h"

#include "Engine/Core/Math/StringHash.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

using namespace CCL::ECS;

// 毎フレームの HashString を避けるための事前計算。
//   グラフ(AnimStateMachine)側は Idle↔Move の遷移条件にこれらを使う:
//     "Speed"    : 実効移動速度（しきい値で Idle/Move を分ける or ブレンド）
//     "IsMoving" : 移動意図があるか（bool で1本の遷移にしたい場合はこちら）
static const uint32_t HASH_SPEED     = CCL::Utils::HashString("Speed");
static const uint32_t HASH_IS_MOVING = CCL::Utils::HashString("IsMoving");

void BossAnimSyncSystem::Update(float /*dt*/)
{
    ForEach([&](const BossActionComponent &boss, AnimParametersComponent &anim) {
        // Move 中だけ「移動」とみなす。Melee/None は停止（Speed=0 → Idle）。
        //   ※攻撃(Slash)は MeleeAttackAction のトリガーが上位で再生するので、
        //     ここで Speed=0(=Idleベース) でも問題ない。
        const bool isMoving = (boss.currentState == BossActionState::Move);

        anim.SetFloat(HASH_SPEED, boss.currentMoveSpeed);
        anim.SetBool(HASH_IS_MOVING, isMoving);
    });
}

// CharacterAnimSyncSystem(L02_PreUpdate=150) の後に走り、ボスの Speed を
// 「意図ベース」で上書きする（物理速度のジッタを排除）。
// アニメ評価(後段)はこの最終値を読む。
REGISTER_LOGIC_SYSTEM(BossAnimSyncSystem, Priority::LogicStage::L02_Update);
