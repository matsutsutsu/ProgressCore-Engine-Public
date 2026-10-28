#include "BossAnimSyncSystem.h"

#include "Engine/Core/Math/StringHash.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

using namespace CCL::ECS;

// パラメータ名のハッシュは先に求めておく。
// グラフは Idle↔Move の遷移条件に
// "Speed"（移動の速さ）と "IsMoving"（移動中か）を使う。
static const uint32_t HASH_SPEED     = CCL::Utils::HashString("Speed");
static const uint32_t HASH_IS_MOVING = CCL::Utils::HashString("IsMoving");

// 横歩きの向き。-1=左回り / 0=横歩きしていない / +1=右回り。
// グラフは Idle・Move から StrafeLeft / StrafeRight への遷移条件にこれを使う。
// 真偽2つでなく1つの float なのは、
// 「左でも右でもある」という有り得ない状態を書けなくするため。
static const uint32_t HASH_STRAFE_DIR = CCL::Utils::HashString("StrafeDir");

// 跳躍の「踏み切り前の溜め〜着地の瞬間」。
// グラフは LeapAir→LeapLand を、これが false になったときに遷移する。
// 滞空の長さは技ごとに違うので、クリップの長さでは着地を決められない。
static const uint32_t HASH_IS_LEAPING = CCL::Utils::HashString("IsLeaping");

// この跳躍は着地で攻撃するか（跳躍攻撃の間ずっと true）。
// グラフは着地のときにこれを見て、攻撃判定を持つ LeapLandAttack と
// 持たない LeapLand を選び分ける。
static const uint32_t HASH_LEAP_ATTACK = CCL::Utils::HashString("LeapAttack");

// 突進の段。0 は突進していない、
// 1〜7 が BossDashPhase の Retreat〜Recovery（+1 したもの）。
//
// 突進開始のクリップ（DashCharge 状態）は構え（3）から流し始め、
// 突っ込む（4）の間も続ける。
// 猛スピードが出る前に構えを見せ、それを予告として読ませるため。
//
// グラフは「しきい値より大きくなったら次の状態へ」で繋いでいるので、
// 長さ 0 の段が飛ばされても途中で詰まらない。
// 段の長さは技ごとの調整値で変わるので、
// クリップの長さでは切り替えを決められない（跳躍の IsLeaping と同じ理由）。
static const uint32_t HASH_DASH_PHASE = CCL::Utils::HashString("DashPhase");

// 後方ステップの段。0 は後方ステップしていない、
// 1〜3 が BossBackStepPhase の Retreat〜Recovery（+1 したもの）。
// 下がる長さは BT の全体時間から逆算するので、
// 突進と同じくクリップの長さでは切り替えを決められない。
static const uint32_t HASH_BACKSTEP_PHASE = CCL::Utils::HashString("BackStepPhase");

void BossAnimSyncSystem::Update(float /*dt*/)
{
    ForEach([&](const BossActionComponent &boss, AnimParametersComponent &anim) {
        // --- 移動 ---

        // Move 中だけ移動とみなす。
        // 技は BossActionSystem が引くトリガーで再生するので、
        // ここで Speed=0（Idle のまま）でも問題ない。
        const bool isMoving = (boss.currentState == BossActionState::Move);

        anim.SetFloat(HASH_SPEED, boss.currentMoveSpeed);
        anim.SetBool(HASH_IS_MOVING, isMoving);

        // --- 横歩き ---

        const bool strafe = (boss.currentState == BossActionState::Strafe);
        anim.SetFloat(HASH_STRAFE_DIR, strafe ? boss.strafeSign : 0.0f);

        // --- 跳躍（着地の瞬間まで IsLeaping を立てる）---

        const bool leap      = (boss.currentState == BossActionState::Leap);
        const bool isLeaping = leap && (boss.leapPhase != BossLeapPhase::Recovery);
        anim.SetBool(HASH_IS_LEAPING, isLeaping);
        anim.SetBool(HASH_LEAP_ATTACK, leap && boss.leapLandAttack);

        // --- 突進・後方ステップ（段の番号を +1 して書く。0 はその技でない）---

        const bool dash = (boss.currentState == BossActionState::Dash);
        anim.SetFloat(HASH_DASH_PHASE, dash ? static_cast<float>(boss.dashPhase) + 1.0f : 0.0f);

        const bool backStep = (boss.currentState == BossActionState::BackStep);
        anim.SetFloat(HASH_BACKSTEP_PHASE, backStep ? static_cast<float>(boss.backStepPhase) + 1.0f : 0.0f);
    });
}

// AnimParameters を読む AnimControllerSystem(L02_Update) より前に書き終える。
// PlayerAnimParamSyncSystem と同じ L02_PreUpdate に置く。
REGISTER_LOGIC_SYSTEM(BossAnimSyncSystem, Priority::LogicStage::L02_PreUpdate);
