#pragma once
// ===========================================================================
// 概要: プレイヤーの回避ダッシュを処理する System。
//
// 責務:
//   - ダッシュの開始・継続・終了と、ジャスト回避の受付時間を管理する
//   - 回避中は HealthComponent の無敵タイマーを保つ
//   - ダッシュが終わったら走行の状態（isRunning）にする
//   - ダッシュ中はカメラへ画角を広げる要求を書く
//
// 備考: ダッシュしていないときの歩き／走りの速さは、この後に走る PlayerMoveSystem が書く。
//       ジャスト回避成立後の「ラッシュ」は PlayerRushSystem が駆動する（別System）。
//       PlayerRushSystem はさらにその後に走り、ラッシュ中のフレームは移動を上書きする。
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Time/Components/TimeState.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/Physics/Components/Character/CharacterMovementComponent.h"
#include "Game/Logics/Character/Player/Dash/PlayerDashComponent.h"
#include "Game/Logics/Character/Player/Core/PlayerComponent.h"
#include "Game/Logics/Character/Player/Core/PlayerInputComponent.h"

class PlayerDashSystem : public CCL::ECS::IfSystem<PlayerDashSystem,
                                                   CCL::ECS::Write<CharacterMovementComponent>,
                                                   CCL::ECS::Write<PlayerDashComponent>,
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
    void MaintainInvincibility(CCL::ECS::EntityID id, const PlayerDashComponent &dash);

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
     */
    void UpdateDash(PlayerDashComponent &dash, PlayerComponent &player, bool dashPressed,
                    CharacterMovementComponent &movement, const TransformComponent &trans,
                    const TimeStateComponent &time, CCL::ECS::EntityID id);

    // このプレイヤーを映す TPS カメラへ、画角を広げる要求を書く。
    void PushWideFovRequest(CCL::ECS::EntityID playerID, bool wide);
};
