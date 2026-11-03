#pragma once
// ===========================================================================
// 概要: プレイヤーの回避ダッシュを処理する System。
//
// 責務:
//   - ダッシュの開始・継続・終了と、ジャスト回避の受付を管理する
//   - 回避中は HealthComponent の無敵タイマーを保つ
//   - ダッシュが終わったら走りに移す（isRunning）
//   - ダッシュ中はカメラへ画角を広げる要求を書く
//
// 備考:
//   ダッシュしていないときの速さは、この後に走る PlayerMoveSystem が書く。
//   ジャスト回避が成立したら、PlayerRushSystem がダッシュを打ち切ってラッシュを始める。
//   ラッシュ中は新しいダッシュを始めない（その場の連撃を、回避で抜けさせない）。
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Time/Components/TimeState.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/Physics/Components/Character/CharacterMovementComponent.h"
#include "Game/Logics/Character/Player/Dash/PlayerDashComponent.h"
#include "Game/Logics/Character/Player/Core/PlayerComponent.h"
#include "Game/Logics/Character/Player/Core/PlayerInputComponent.h"
#include "Game/Logics/Character/Player/Rush/PlayerRushComponent.h"

class PlayerDashSystem : public CCL::ECS::IfSystem<PlayerDashSystem,
                                                   CCL::ECS::Write<CharacterMovementComponent>,
                                                   CCL::ECS::Write<PlayerDashComponent>,
                                                   CCL::ECS::Write<PlayerComponent>,
                                                   CCL::ECS::Read<PlayerInputComponent>,
                                                   CCL::ECS::Read<TransformComponent>,
                                                   CCL::ECS::Read<TimeStateComponent>,
                                                   CCL::ECS::Read<PlayerRushComponent>>
{
public:
    PlayerDashSystem() : IfSystem("PlayerDashSystem") {}
    void Update(float dt) override;

private:
    // 回避中は、被弾処理が見る HealthComponent の無敵タイマーを invincibleWindow 以上に保つ
    void MaintainInvincibility(CCL::ECS::EntityID id, const PlayerDashComponent &dash);

    /**
     * @brief ダッシュの開始・継続・終了を1ステップ分進める。終わったら走りに移す。
     * @param dash        ダッシュの状態。
     * @param player      走りと locomotion を書く先。
     * @param dashPressed このステップにダッシュを押した瞬間か。
     * @param movement    向きと速さを書く先。開始時は desiredVelocity を向きに使う。
     * @param trans       入力が無いときの向きと、イベントの位置に使う。
     * @param time        localDt でタイマーを進める。
     * @param id          プレイヤーのエンティティ。
     */
    void UpdateDash(PlayerDashComponent &dash, PlayerComponent &player, bool dashPressed,
                    CharacterMovementComponent &movement, const TransformComponent &trans,
                    const TimeStateComponent &time, CCL::ECS::EntityID id);

    // このプレイヤーを映す TPS カメラへ、画角を広げる要求を書く
    void PushWideFovRequest(CCL::ECS::EntityID playerID, bool wide);
};
