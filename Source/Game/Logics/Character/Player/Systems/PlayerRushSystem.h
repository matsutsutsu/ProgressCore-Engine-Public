#pragma once
// ===========================================================================
// 概要: ジャスト回避成立後の「ラッシュ」（踏込→固定→集中スロー＋間合い詰め）を駆動する System。
//
// 責務:
//   - RushComponent のフェーズ（Lunge/Plant/Focus）を実時間で進める
//   - ラッシュ中は世界スローを維持し、cinematicScale でプレイヤーだけ実時間へ戻す
//   - 集中スロー中、攻撃で入った寄せ要求(closeIn)に従って対象へ詰める
//   - ラッシュ中のビネット演出と無敵フラグを立て、終了時に後片付けする
//
// 備考: 回避ダッシュ本体と歩走は PlayerDashSystem（別System）。開始の号砲は JustEvadeSystem。
//       PlayerDashSystem より先に走り、動きをロックするフレームは
//       PlayerDashSystem 側が RushComponent::LocksMovement() を見てスキップする。
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Time/Components/TimeState.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/Physics/Components/Character/CharacterMovementComponent.h"
#include "Game/Logics/Character/Player/Components/DashComponent.h"
#include "Game/Logics/Character/Player/Components/RushComponent.h"

// ポインタでしか使わないので前方宣言にする（.cpp で include する）。
struct RootMotionComponent;

class PlayerRushSystem : public CCL::ECS::IfSystem<PlayerRushSystem,
                                                   CCL::ECS::Write<CharacterMovementComponent>,
                                                   CCL::ECS::Write<DashComponent>,
                                                   CCL::ECS::Write<RushComponent>,
                                                   CCL::ECS::Write<TimeStateComponent>,
                                                   CCL::ECS::Read<TransformComponent>>
{
public:
    PlayerRushSystem() : IfSystem("PlayerRushSystem") {}
    void Update(float dt) override;

    // ラッシュのビネット演出を切り替える JustEvadeEffectComponent(singleton) への書き込みを申告する。
    std::vector<CCL::ECS::TypeID> GetWriteTypes() const override;

private:
    // 世界スローを維持しつつ cinematicScale でプレイヤーだけ実時間へ戻す（全フェーズ共通）。
    void SustainSlow(RushComponent &rush, TimeStateComponent &time, const TimeContext *timeCtx);

    /**
     * @brief 集中スロー中の「間合い詰め(closeIn)」の寄せを進める。
     *
     * closeInTimer>0 の間だけ、対象(closeInTargetID)へ毎フレーム方向を取り直して寄せ、
     * 近接の間合い(closeInStopRange)に入るか時間切れ・対象消失で寄せを終える。
     *
     * @return 寄せている最中なら true（そのフレームは移動を上書き）。寄せ無しなら false。
     */
    bool DriveCloseIn(CCL::ECS::EntityID id, const DirectX::SimpleMath::Vector3 &playerPos,
                      RushComponent &rush, CharacterMovementComponent &movement,
                      RootMotionComponent *rootMotion, float rawDt);

    // ラッシュを終える（スロー打ち消し停止・ビネット降ろし・無敵解除・クールダウン付与）。
    void End(DashComponent &dash, RushComponent &rush, TimeStateComponent &time);

    // ビネット演出を切り替える（JustEvadeEffectComponent.rushActive を叩く）。
    void SetVignette(bool on);
};
