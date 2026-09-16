// ボスの大状態（第1形態 / 移行演出 / 第2形態 / 死亡）を決める判断層。
// 責務は「大状態の遷移」と「入場した瞬間だけの処理」の2つに限る。
//
//   BossPerceptionSystem … 世界を見る（認識）      L01_Input
//   BossPhaseSystem      … 大状態を決める（判断）  L02_PreUpdate ← このファイル
//   BehaviorTreeSystem   … 技を選ぶ（戦術）        L02_Update
//   BossActionSystem     … 物理に変換する（実行）  L02_PostUpdate
//
// 汎用ステートマシンを使わないのは、BTの root Selector が既に「優先度順の状態選択」
// そのものだから。BTで書けない「入った瞬間に1回だけ」（BTの差し替え・無敵付与・
// アニメ発火）のためだけにこの小さなFSMを置いている。
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

    // 知覚の後に走らせる。両者とも L02_PreUpdate で BehaviorTreeComponent を Write
    // するので、触る場所が重ならなくてもスケジューラには順序不定と報告される。
    std::vector<const char *> GetRunAfter() const override
    {
        return {"BossPerceptionSystem"};
    }

    void Update(float dt) override;

private:
    /// 今フレームの遷移先を決める。優先度順で、上から最初に成立したものが勝つ。
    BossPhase DecideNextPhase(const BossPhaseComponent &phase,
                              const HealthComponent    &health) const;

    /// 状態に入った瞬間だけ走る処理。BTでは書けないのがこの部分。
    void OnEnterPhase(BossPhase                next,
                      BossPhaseComponent      &phase,
                      BehaviorTreeComponent   &bt,
                      BossCommandComponent    &cmd,
                      AnimParametersComponent &anim,
                      HealthComponent         &health) const;

    /// 状態に滞在している間、毎フレーム走る処理。
    void TickPhase(const BossPhaseComponent &phase,
                   BossCommandComponent     &cmd,
                   HealthComponent          &health) const;

    // どちらも Muriel の AnimGraph にステートが無いので、今は発火しても何も起きない
    // （追加すればそのまま繋がる）。アニメ無しでも移行そのものは成立する。
    static constexpr const char *kDeathTrigger = "Death";
    static constexpr const char *kRoarTrigger  = "Roar";

    // 形態移行の演出時間 [秒]。この間ボスは無敵で、BTは止まる。
    static constexpr float kTransitionDuration = 2.5f;
};
