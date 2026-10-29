#include "BossAnimSyncSystem.h"

#include "Engine/Core/Math/StringHash.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

using namespace CCL::ECS;

// アニメグラフの遷移条件に使うパラメータ
static const uint32_t HASH_IS_MOVING      = CCL::Utils::HashString("IsMoving");      // 歩いて近づいているか
static const uint32_t HASH_STRAFE_DIR     = CCL::Utils::HashString("StrafeDir");     // 横歩き。-1=左 / 0=なし / +1=右
static const uint32_t HASH_IS_LEAPING     = CCL::Utils::HashString("IsLeaping");     // 踏み切り前〜着地の瞬間
static const uint32_t HASH_LEAP_ATTACK    = CCL::Utils::HashString("LeapAttack");    // 着地で攻撃する跳躍か
static const uint32_t HASH_DASH_PHASE     = CCL::Utils::HashString("DashPhase");     // 突進の段 +1。0=突進していない
static const uint32_t HASH_BACKSTEP_PHASE = CCL::Utils::HashString("BackStepPhase"); // 後方ステップの段 +1。0=していない

void BossAnimSyncSystem::Update(float /*dt*/)
{
    ForEach([&](const BossActionComponent &boss, AnimParametersComponent &anim) {
        // --- 移動 ---

        const bool isMoving = (boss.currentState == BossActionState::Move);
        anim.SetBool(HASH_IS_MOVING, isMoving);

        // --- 横歩き ---

        const bool strafe = (boss.currentState == BossActionState::Strafe);
        anim.SetFloat(HASH_STRAFE_DIR, strafe ? boss.strafeSign : 0.0f);

        // --- 跳躍（着地の瞬間まで IsLeaping を立てる）---

        const bool leap      = (boss.currentState == BossActionState::Leap);
        const bool isLeaping = leap && (boss.leapPhase != BossLeapPhase::Recovery);
        anim.SetBool(HASH_IS_LEAPING, isLeaping);
        anim.SetBool(HASH_LEAP_ATTACK, leap && boss.leapDef.landAttack);

        // --- 突進・後方ステップ ---

        // 段の長さは調整値で変わるので、クリップの長さでなく段の番号で切り替えさせる
        const bool dash = (boss.currentState == BossActionState::Dash);
        anim.SetFloat(HASH_DASH_PHASE, dash ? static_cast<float>(boss.dashPhase) + 1.0f : 0.0f);

        const bool backStep = (boss.currentState == BossActionState::BackStep);
        anim.SetFloat(HASH_BACKSTEP_PHASE, backStep ? static_cast<float>(boss.backStepPhase) + 1.0f : 0.0f);
    });
}

// AnimParameters を読む AnimControllerSystem(L02_Update) より前に書く
REGISTER_LOGIC_SYSTEM(BossAnimSyncSystem, Priority::LogicStage::L02_PreUpdate);
