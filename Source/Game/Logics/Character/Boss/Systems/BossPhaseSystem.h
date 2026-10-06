#pragma once
// ===========================================================================
// 概要: ボスの形態（第1形態 / 移行演出 / 第2形態 / 死亡）を決める System。
//
// 責務:
//   - HP と経過時間から、次の形態を決める
//   - 形態に入った瞬間だけの処理（BT の差し替え・無敵・アニメのトリガー）を行う
//   - 移行演出中と死亡中は、ボス本体とドローンへの指示を止め続ける
//
// 備考:
//   ボスの System の並び:
//     BossPerceptionSystem … blackboard へ判断材料を書く  L02_PreUpdate
//     BossPhaseSystem      … 形態を決める                  L02_PreUpdate（Perception の後）← このファイル
//     BehaviorTreeSystem   … 技を選ぶ                      L02_Update
//     BossActionSystem     … 体の動きに変える              L02_PostUpdate
//
//   汎用のステートマシンを使わないのは、BT の root Selector が既に優先度順の状態選択に
//   なっているから。BT で書けない「入った瞬間に1回だけ」の処理のためだけに、この小さな FSM を置いている。
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include <vector>
#include "Game/Logics/Character/Boss/Components/BossPhaseComponent.h"
#include "Game/Logics/AI/BehaviorTree/Data/BehaviorTreeComponents.h" // BehaviorTreeComponent
#include "Game/Logics/Character/Boss/Components/BossCommandComponent.h"
#include "Game/Logics/Combat/Components/HealthComponent.h"
#include "Engine/Animation/Components/AnimParametersComponent.h"
#include "Engine/Time/Components/TimeState.h"

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

    /**
     * @brief 同じステージで先に走らせる System を宣言する。
     *
     * BossPerceptionSystem の後に走らせる。
     * どちらも L02_PreUpdate で BehaviorTreeComponent を Write するので、触るフィールドが重ならなくても、スケジューラには順序が決まっていないと報告される。
     */
    std::vector<const char *> GetRunAfter() const override
    {
        return {"BossPerceptionSystem"};
    }

    void Update(float dt) override;

private:
    // このフレームの遷移先を決める。優先度順に調べ、最初に成立したものを返す。
    BossPhase DecideNextPhase(const BossPhaseComponent &phase,
                              const HealthComponent    &health) const;

    // 形態に入った瞬間だけ走る処理（BT では書きにくい部分）。
    void OnEnterPhase(BossPhase                next,
                      BossPhaseComponent      &phase,
                      BehaviorTreeComponent   &bt,
                      BossCommandComponent    &cmd,
                      AnimParametersComponent &anim,
                      HealthComponent         &health) const;

    // 形態に留まっている間、毎フレーム走る処理。
    void TickPhase(const BossPhaseComponent &phase,
                   BossCommandComponent     &cmd,
                   HealthComponent          &health) const;

    // どちらも Muriel のアニメグラフに対応する遷移が無いので、今はトリガーを引いても何も起きない
    // （グラフに足せばそのまま繋がる）。アニメが無くても形態の移行はできる。
    static constexpr const char *kDeathTrigger = "Death";
    static constexpr const char *kRoarTrigger  = "Roar";

    // 形態移行の演出時間（秒）。この間ボスは無敵で、BT は止まる。
    static constexpr float kTransitionDuration = 2.5f;
};
