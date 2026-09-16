#pragma once
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Time/TimeState.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/Physics/Components/Character/CharacterMovementComponent.h"
#include "Game/Logics/Character/Player/Components/DashComponent.h"
#include "Game/Logics/Character/Player/Components/PlayerComponent.h"
#include "Game/Logics/Character/Player/Components/PlayerInputComponent.h"
#include "Game/Logics/Character/Player/Components/CleaveComponent.h" // 切り払い中は回避を開始させない

// ポインタ参照のみのため前方宣言で足りる（実体は .cpp で include）。
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
    // 回避(ダッシュ/接近)中、被弾処理が見る無敵タイマー(HealthComponent.invincibilityTimer)を維持する。
    void MaintainInvincibility(CCL::ECS::EntityID id, const DashComponent &dash);

    // 接近（ジャスト回避スロー中にボスへ詰める専用時間）を処理する。
    // 接近中だった場合は true を返し、呼び出し側はこのフレームの残り処理をスキップする。
    bool UpdateApproach(float rawDt, const BattleContext *battle, const TimeContext *timeCtx,
                        DashComponent &dash, CharacterMovementComponent &movement,
                        const TransformComponent &trans, TimeStateComponent &time,
                        RootMotionComponent *rm);

    // ダッシュ（回避バースト）の開始/継続/終了→走行移行を処理する。
    // このフレームをダッシュとして消費したら true（呼び出し側は地上ロコモーションを行わない）。
    bool UpdateDash(DashComponent &dash, PlayerComponent &player, bool dashPressed,
                    CharacterMovementComponent &movement, const TransformComponent &trans,
                    const TimeStateComponent &time, CCL::ECS::EntityID id);

    // 非ダッシュ時の地上ロコモーション（走行/歩き）。走行はトグル＋無入力で自動解除。
    void UpdateGroundLocomotion(PlayerComponent &player, CharacterMovementComponent &movement, float dt);

    // このプレイヤーを映す TPS カメラへ「画角を広げてほしい」要求を push する。
    void PushWideFovRequest(CCL::ECS::EntityID playerID, bool wide);

    // 接近を終了し、プレイヤーの時計を通常へ戻す共通処理。
    void EndApproach(DashComponent &dash, TimeStateComponent &time);
};
