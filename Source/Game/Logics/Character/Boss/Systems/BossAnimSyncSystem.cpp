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

// 跳躍の「踏み切り前の溜め〜着地の瞬間」。グラフは LeapAir→LeapLand をこれが false に
// なったときに遷移する。滞空の長さは技ごとに違うので、クリップの長さでは着地を決められない。
static const uint32_t HASH_IS_LEAPING = CCL::Utils::HashString("IsLeaping");

// 着地攻撃の振り下ろしが始まった（着地の少し前から true）。グラフは IsLeaping より先に
// これを見るので、着地攻撃のときは空中ループから LeapLand ではなく LeapSlam へ抜ける。
static const uint32_t HASH_LEAP_STRIKE = CCL::Utils::HashString("LeapStrike");

void BossAnimSyncSystem::Update(float /*dt*/)
{
    ForEach([&](const BossActionComponent &boss, AnimParametersComponent &anim) {
        // Move 中だけ「移動」とみなす。技は TriggeredMotionAction のトリガーが上位で
        // 再生するので、ここが Speed=0（Idleベース）でも問題ない。
        const bool isMoving = (boss.currentState == BossActionState::Move);

        anim.SetFloat(HASH_SPEED, boss.currentMoveSpeed);
        anim.SetBool(HASH_IS_MOVING, isMoving);

        const bool leap      = (boss.currentState == BossActionState::Leap);
        const bool isLeaping = leap && (boss.leapPhase == BossLeapPhase::Windup
                                        || boss.leapPhase == BossLeapPhase::Flight);
        anim.SetBool(HASH_IS_LEAPING, isLeaping);
        anim.SetBool(HASH_LEAP_STRIKE, leap && boss.leapPhase == BossLeapPhase::Strike);
    });
}

// 掲示板への書き込みは消費側 AnimControllerSystem(L02_Update) より前に済ませる。
// Player 側の掲示板書き手と同じ L02_PreUpdate に置く。
REGISTER_LOGIC_SYSTEM(BossAnimSyncSystem, Priority::LogicStage::L02_PreUpdate);
