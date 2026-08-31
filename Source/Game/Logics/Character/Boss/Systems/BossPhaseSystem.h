/// @file  BossPhaseSystem.h
/// @brief ボスの大状態（第1形態 / 移行演出 / 第2形態 / 死亡）を決める判断層。
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
///   状態が4つで、ボスは1体しか存在しない。BTの root Selector が既に
///   「優先度順の状態選択」そのものなので、木構造の機構を別に持つ意味がない。
///   BTで書けない唯一の部分が「入った瞬間に1回だけ」（BTの差し替え・無敵付与・
///   アニメ発火）で、それだけのためにこの小さなFSMを置いている。
///
/// 実行順序: L02_PreUpdate。BehaviorTreeSystem(L02_Update) が assetPath を読む前に
///   大状態を確定させる必要があるため、ここは動かせない。
#pragma once
#include "Engine/ECS/System/CCL_IfSystem.h"
#include <vector>
#include "Game/Logics/Character/Boss/Components/BossPhaseComponent.h"
#include "Game/Logics/AI/BehaviorTree/Data/BehaviorTreeComponents.h" // BehaviorTreeComponent
#include "Game/Logics/Character/Boss/Components/BossCommandComponent.h"
#include "Game/Logics/Combat/HealthComponent.h"
#include "Engine/Animation/Components/AnimParametersComponent.h"
#include "Engine/Time/TimeState.h"

class BossPhaseSystem : public CCL::ECS::IfSystem<
                            BossPhaseSystem,
                            CCL::ECS::Write<BossPhaseComponent>,
                            CCL::ECS::Write<BehaviorTreeComponent>,
                            CCL::ECS::Write<BossCommandComponent>,
                            CCL::ECS::Write<AnimParametersComponent>,
                            CCL::ECS::Write<HealthComponent>,
                            CCL::ECS::Read<TimeStateComponent>>
{
  public:
    BossPhaseSystem()
        : IfSystem("BossPhaseSystem")
    {
    }
    ~BossPhaseSystem() override = default;

    /// @brief 知覚の後に走らせる。
    ///
    /// なぜ宣言が要るか:
    ///   BossPerceptionSystem も L02_PreUpdate にいて、両者とも
    ///   BehaviorTreeComponent を Write する（あちらは blackboard、こちらは assetPath）。
    ///   TaskScheduler は型の粒度でしか見ないので、実際には触る場所が重ならなくても
    ///   「競合するのに前後が宣言されていない」＝順序不定として起動時に報告される。
    ///   今は順序が変わっても結果は同じだが、
    ///   将来ここで blackboard.healthPercentage を見たくなったときに
    ///   「感知 → 大状態」の順でないと成立しないので、その向きで固定しておく。
    std::vector<const char *> GetRunAfter() const override
    {
        return {"BossPerceptionSystem"};
    }

    void Update(float dt) override;

  private:
    /// @brief 今フレームの遷移先を決める。優先度順で、上から最初に成立したものが勝つ。
    /// @note  移行の条件（第2形態のBTパスとHP閾値）は BossPhaseComponent が持つ。
    ///        以前は BehaviorTreeComponent 側にあったが、あれは汎用データの置き場なので、
    ///        ボスにしか無い概念は読み手と同じ層へ移した。
    BossPhase DecideNextPhase(const BossPhaseComponent &phase,
                              const HealthComponent    &health) const;

    /// @brief 状態に入った瞬間だけ走る処理。BTでは書けないのがこの部分。
    void OnEnterPhase(BossPhase                next,
                      BossPhaseComponent      &phase,
                      BehaviorTreeComponent   &bt,
                      BossCommandComponent    &cmd,
                      AnimParametersComponent &anim,
                      HealthComponent         &health) const;

    /// @brief 状態に滞在している間、毎フレーム走る処理。
    void TickPhase(const BossPhaseComponent &phase,
                   BossCommandComponent     &cmd,
                   HealthComponent          &health) const;

    /// @brief 死亡アニメのトリガー名。
    /// @note  現状 Muriel の AnimGraph に Death ステートが無いため、この発火は
    ///        何も起こさない（アニメを追加すればそのまま繋がる）。
    static constexpr const char *kDeathTrigger = "Death";

    /// @brief 形態移行時の咆哮アニメのトリガー名。
    /// @note  同上。AnimGraph に Roar ステートを足すと演出が乗る。
    ///        アニメが無くても「停止＋無敵＋群れの退避」で移行は成立する。
    static constexpr const char *kRoarTrigger = "Roar";

    /// @brief 形態移行の演出時間（秒）。この間ボスは無敵で、BTは止まる。
    static constexpr float kTransitionDuration = 2.5f;
};
