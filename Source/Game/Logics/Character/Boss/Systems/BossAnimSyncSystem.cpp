// ボスの移動アニメ駆動の実装。BossActionComponent → AnimParameters の翻訳。
#include "BossAnimSyncSystem.h"

#include "Engine/Core/Math/StringHash.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

using namespace CCL::ECS;

// 毎フレームの HashString を避けるための事前計算。グラフ側は Idle↔Move の遷移条件に
// "Speed"（実効移動速度）と "IsMoving"（移動意図）を使う。
static const uint32_t HASH_SPEED     = CCL::Utils::HashString("Speed");
static const uint32_t HASH_IS_MOVING = CCL::Utils::HashString("IsMoving");

void BossAnimSyncSystem::Update(float /*dt*/)
{
    ForEach([&](const BossActionComponent &boss, AnimParametersComponent &anim) {
        // Move 中だけ「移動」とみなす。技は TriggeredMotionAction のトリガーが上位で
        // 再生するので、ここが Speed=0（Idleベース）でも問題ない。
        const bool isMoving = (boss.currentState == BossActionState::Move);

        anim.SetFloat(HASH_SPEED, boss.currentMoveSpeed);
        anim.SetBool(HASH_IS_MOVING, isMoving);
    });
}

// 掲示板への書き込みは消費側 AnimControllerSystem(L02_Update) より前に済ませる。
// Player 側の掲示板書き手と同じ L02_PreUpdate に置く。
REGISTER_LOGIC_SYSTEM(BossAnimSyncSystem, Priority::LogicStage::L02_PreUpdate);
