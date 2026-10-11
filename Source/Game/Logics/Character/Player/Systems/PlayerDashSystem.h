#pragma once
// ===========================================================================
// 概要: プレイヤーの回避ダッシュと、歩き／走りの切り替えを処理する System。
//
// 責務:
//   - ダッシュの開始・継続・終了と、ジャスト回避の受付時間を管理する
//   - 回避中は HealthComponent の無敵タイマーを保つ
//   - ダッシュの後は走行に移り、無入力が続いたら歩きに戻す
//   - ダッシュ中はカメラへ画角を広げる要求を書き、ルートモーションの移動を止める
//
// 備考: ジャスト回避成立後の「ラッシュ」は PlayerRushSystem が駆動する（別System）。
//       PlayerRushSystem はこの System の後に走り、ラッシュ中のフレームは移動を上書きする。
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Time/Components/TimeState.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/Physics/Components/Character/CharacterMovementComponent.h"
#include "Game/Logics/Character/Player/Components/DashComponent.h"
#include "Game/Logics/Character/Player/Components/PlayerComponent.h"
#include "Game/Logics/Character/Player/Components/PlayerInputComponent.h"

// ポインタでしか使わないので前方宣言にする（.cpp で include する）。
struct RootMotionComponent;

class PlayerDashSystem : public CCL::ECS::IfSystem<PlayerDashSystem,
                                                   CCL::ECS::Write<CharacterMovementComponent>,
                                                   CCL::ECS::Write<DashComponent>,
                                                   CCL::ECS::Write<PlayerComponent>,
                                                   CCL::ECS::Read<PlayerInputComponent>,
                                                   CCL::ECS::Read<TransformComponent>,
                                                   CCL::ECS::Read<TimeStateComponent>>
{
public:
    PlayerDashSystem() : IfSystem("PlayerDashSystem") {}
    void Update(float dt) override;

private:
    // 回避中は、被弾処理が見る HealthComponent.invincibilityTimer を invincibleWindow 以上に保つ。
    void MaintainInvincibility(CCL::ECS::EntityID id, const DashComponent &dash);

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
};
