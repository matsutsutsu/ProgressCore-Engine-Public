#pragma once
// ===========================================================================
// 概要: プレイヤーの回避ダッシュ、ジャスト回避後の接近、歩きと走りの切り替えを処理する System。
//
// 責務:
//   - ダッシュの開始・継続・終了と、ジャスト回避の受付時間を管理する
//   - ジャスト回避後の接近で、世界スローを打ち消してボスへ近づける
//   - 回避中は HealthComponent の無敵タイマーを保つ
//   - ダッシュの後は走行に移り、無入力が続いたら歩きに戻す
//   - ダッシュ中はカメラへ画角を広げる要求を書き、ルートモーションの移動を止める
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Time/Components/TimeState.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/Physics/Components/Character/CharacterMovementComponent.h"
#include "Game/Logics/Character/Player/Components/DashComponent.h"
#include "Game/Logics/Character/Player/Components/PlayerComponent.h"
#include "Game/Logics/Character/Player/Components/PlayerInputComponent.h"
#include "Game/Logics/Character/Player/Components/CleaveComponent.h" // 切り払い中はダッシュを始めさせない

// ポインタでしか使わないので前方宣言にする（.cpp で include する）。
struct BattleContext;
struct RootMotionComponent;

class PlayerDashSystem : public CCL::ECS::IfSystem<PlayerDashSystem,
                                                   CCL::ECS::Write<CharacterMovementComponent>,
                                                   CCL::ECS::Write<DashComponent>,
                                                   CCL::ECS::Write<PlayerComponent>,
                                                   CCL::ECS::Read<PlayerInputComponent>,
                                                   CCL::ECS::Read<CleaveComponent>,
                                                   CCL::ECS::Read<TransformComponent>,
                                                   CCL::ECS::Write<TimeStateComponent>>
{
public:
    PlayerDashSystem() : IfSystem("PlayerDashSystem") {}
    void Update(float dt) override;

private:
    // 回避（ダッシュ/接近）中は、被弾処理が見る HealthComponent.invincibilityTimer を invincibleWindow 以上に保つ。
    void MaintainInvincibility(CCL::ECS::EntityID id, const DashComponent &dash);

    /**
     * @brief ジャスト回避後の接近を処理する。
     *
     * 通常のダッシュより優先する。世界はスローのまま、プレイヤーだけ実時間の速さでボスへ近づく。
     *
     * @param rawDt    スローの掛からない実時間の経過（秒）。接近の残り時間を進める。
     * @param battle   ボスの位置を引く戦況。nullptr かボスが居なければ接近を打ち切る。
     * @param timeCtx  全体スローの倍率。nullptr ならプレイヤーのスロー打ち消しを行わない。
     * @param dash     接近の状態を持つダッシュ。
     * @param movement 移動速度を書く先。
     * @param trans    プレイヤーの Transform。
     * @param time     プレイヤーの時間の状態。ヒットストップ中は進めない。
     * @param rm       ルートモーション。nullptr 可。
     * @return 接近中なら true。呼び出し側はこのフレームの残りの処理を飛ばす。
     */
    bool UpdateApproach(float rawDt, const BattleContext *battle, const TimeContext *timeCtx,
                        DashComponent &dash, CharacterMovementComponent &movement,
                        const TransformComponent &trans, TimeStateComponent &time,
                        RootMotionComponent *rm);

    /**
     * @brief ダッシュの開始・継続・終了を処理する。終了時は歩きに戻さず走行に移す。
     *
     * @param dash        ダッシュの状態。
     * @param player      プレイヤーの状態。
     * @param dashPressed このフレームにダッシュの入力があったか。
     * @param movement    移動速度を書く先。
     * @param trans       プレイヤーの Transform。
     * @param time        プレイヤーの時間の状態（localDt でタイマーを進める）。
     * @param id          プレイヤーのエンティティ。
     * @return このフレームをダッシュとして処理したら true（呼び出し側は歩き/走りの処理を行わない）。
     */
    bool UpdateDash(DashComponent &dash, PlayerComponent &player, bool dashPressed,
                    CharacterMovementComponent &movement, const TransformComponent &trans,
                    const TimeStateComponent &time, CCL::ECS::EntityID id);

    // ダッシュしていないときの歩き/走り。走行中は、無入力が runIdleReleaseTime 続いたら歩きに戻す。
    void UpdateGroundLocomotion(PlayerComponent &player, CharacterMovementComponent &movement, float dt);

    // このプレイヤーを映す TPS カメラへ、画角を広げる要求を書く。
    void PushWideFovRequest(CCL::ECS::EntityID playerID, bool wide);

    // 接近を終え、プレイヤーの時間倍率を元に戻す。
    void EndApproach(DashComponent &dash, TimeStateComponent &time);
};
