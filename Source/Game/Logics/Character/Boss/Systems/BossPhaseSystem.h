/// @file  BossPhaseSystem.h
/// @brief ボスの大状態（戦闘中／死亡）を決める判断層。
///
/// 責務は「大状態の遷移」と「入場した瞬間だけの処理」の2つに限る。
/// 具体的な技の選択はボスBTが、物理への変換は BossActionSystem が担う。
///
/// ボスの層構成:
///   BossPerceptionSystem … 世界を見る（認識）      L01_Input
///   BossPhaseSystem      … 大状態を決める（判断）  L02_PreUpdate ← このファイル
///   BehaviorTreeSystem   … 技を選ぶ（戦術）        L02_Update
///   BossActionSystem     … 物理に変換する（実行）  L02_PostUpdate
///
/// なぜ汎用ステートマシンを使わないか:
///   状態が2つで、ボスは1体しか存在しない。BTの root Selector が既に
///   「優先度順の状態選択」そのものなので、木構造の機構を別に持つ意味がない。
///   BTで書けない唯一の部分が「入った瞬間に1回だけ」（死亡アニメの発火など）で、
///   それだけのためにこの小さなFSMを置いている。
///
/// 実行順序: L02_PreUpdate。BehaviorTreeSystem(L02_Update) が assetPath を読む前に
///   大状態を確定させる必要があるため、ここは動かせない。
#pragma once
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Game/Logics/Character/Boss/Data/BossPhaseComponent.h"
#include "Game/Logics/AI/BehaviorTree/Data/BehaviorTreeComponents.h" // BossCommandComponent
#include "Game/Logics/Combat/HealthComponent.h"
#include "Engine/Animation/Components/AnimParametersComponent.h"
#include "Engine/Time/TimeState.h"

class BossPhaseSystem : public CCL::ECS::IfSystem<
                            BossPhaseSystem,
                            CCL::ECS::Write<BossPhaseComponent>,
                            CCL::ECS::Write<BossCommandComponent>,
                            CCL::ECS::Write<AnimParametersComponent>,
                            CCL::ECS::Read<HealthComponent>,
                            CCL::ECS::Read<TimeStateComponent>>
{
  public:
    BossPhaseSystem()
        : IfSystem("BossPhaseSystem")
    {
    }
    ~BossPhaseSystem() override = default;

    void Update(float dt) override;

  private:
    /// @brief 今フレームの遷移先を決める。優先度順で、上から最初に成立したものが勝つ。
    /// @note  第2形態を足すときはここに分岐を1〜2行足す。
    BossPhase DecideNextPhase(const BossPhaseComponent &phase,
                              const HealthComponent    &health) const;

    /// @brief 状態に入った瞬間だけ走る処理。BTでは書けないのがこの部分。
    void OnEnterPhase(BossPhase                 next,
                      BossPhaseComponent       &phase,
                      BossCommandComponent     &cmd,
                      AnimParametersComponent  &anim) const;

    /// @brief 状態に滞在している間、毎フレーム走る処理。
    void TickPhase(const BossPhaseComponent &phase, BossCommandComponent &cmd) const;

    /// @brief 死亡アニメのトリガー名。AnimGraph 側のトリガー名と一致させること。
    static constexpr const char *kDeathTrigger = "Death";
};
