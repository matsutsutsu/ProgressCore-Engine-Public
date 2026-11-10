#include "BossAnimSyncSystem.h"

#include "Engine/Core/Math/StringHash.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

using namespace CCL::ECS;

// アニメグラフの遷移条件に使うパラメータ
static const uint32_t HASH_IS_MOVING         = CCL::Utils::HashString("IsMoving");        // 歩いて近づいているか
static const uint32_t HASH_SIDE_TRAVEL_PHASE = CCL::Utils::HashString("SideTravelPhase"); // 横移動の段 +1。0=横移動していない
static const uint32_t HASH_IS_LEAPING        = CCL::Utils::HashString("IsLeaping");       // 踏み切り前〜着地の瞬間
static const uint32_t HASH_LEAP_ATTACK       = CCL::Utils::HashString("IsLeapAttack");      // 着地で攻撃する跳躍か
static const uint32_t HASH_DASH_PHASE        = CCL::Utils::HashString("DashPhase");       // 突進の段 +1。0=突進していない
static const uint32_t HASH_BACKSTEP_PHASE    = CCL::Utils::HashString("BackStepPhase");   // 後方ステップの段 +1。0=していない
static const uint32_t HASH_FOOTWORK_DIR      = CCL::Utils::HashString("FootworkDir");     // 様子見の向き。0=していない 1=前 2=後ろ 3=左 4=右
static const uint32_t HASH_CAN_FLINCH        = CCL::Utils::HashString("CanFlinch");       // ひるみの冷却が明けているか
static const uint32_t HASH_MOVE_X            = CCL::Utils::HashString("MoveX");           // 体から見た動く向きの右成分（-1〜1）
static const uint32_t HASH_MOVE_Y            = CCL::Utils::HashString("MoveY");           // 体から見た動く向きの前成分（-1〜1）

void BossAnimSyncSystem::Update(float /*dt*/)
{
    ForEach([&](const BossActionComponent &boss, AnimParametersComponent &anim) {
        // --- 移動 ---

        const bool isMoving = (boss.currentState == BossActionState::Move);
        anim.SetBool(HASH_IS_MOVING, isMoving);

        // --- 跳躍（着地の瞬間まで IsLeaping を立てる）---

        const bool leap      = (boss.currentState == BossActionState::Leap);
        const bool isLeaping = leap && (boss.leapPhase != BossLeapPhase::Recovery);
        anim.SetBool(HASH_IS_LEAPING, isLeaping);
        anim.SetBool(HASH_LEAP_ATTACK, leap && boss.leapDef.landAttack);

        // --- 横移動・突進・後方ステップ ---

        // 段の長さは調整値や距離で変わるので、クリップの長さでなく段の番号で切り替えさせる
        const bool sideTravel = (boss.currentState == BossActionState::SideTravel);
        anim.SetFloat(HASH_SIDE_TRAVEL_PHASE, sideTravel ? static_cast<float>(boss.sideTravelPhase) + 1.0f : 0.0f);

        const bool dash = (boss.currentState == BossActionState::Dash);
        anim.SetFloat(HASH_DASH_PHASE, dash ? static_cast<float>(boss.dashPhase) + 1.0f : 0.0f);

        const bool backStep = (boss.currentState == BossActionState::BackStep);
        anim.SetFloat(HASH_BACKSTEP_PHASE, backStep ? static_cast<float>(boss.backStepPhase) + 1.0f : 0.0f);

        // --- 様子見 ---

        // 前後左右のどのループを流すか。BossFootworkDir の並び（None=0）をそのまま番号にする
        // 様子見から抜けたら、向きが残っていても 0 にする
        const bool footwork = (boss.currentState == BossActionState::Footwork);
        anim.SetFloat(HASH_FOOTWORK_DIR, footwork ? static_cast<float>(boss.footworkAnimDir) : 0.0f);

        // 様子見と横移動のブレンドスペースは、前後左右のクリップを体から見た動く向きで混ぜる
        //   FootworkDir は様子見に入る・出るの判定にだけ使う（0 = していない）
        const bool directional = footwork || sideTravel;
        anim.SetFloat(HASH_MOVE_X, directional ? boss.localMoveDirection.x : 0.0f);
        anim.SetFloat(HASH_MOVE_Y, directional ? boss.localMoveDirection.y : 0.0f);

        // --- ひるみ ---

        // 冷却中はひるませない。技の最中にひるまないのは、グラフの AnyState が group 1 に絞っているため
        anim.SetBool(HASH_CAN_FLINCH, boss.flinchCooldownTimer <= 0.0f);
    });
}



// AnimParameters を読む AnimControllerSystem(L02_Update) より前に書く
REGISTER_LOGIC_SYSTEM(BossAnimSyncSystem, Priority::LogicStage::L02_PreUpdate);
